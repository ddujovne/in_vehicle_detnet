#include "EtherMACFullDuplex.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <iostream>
#include <string>
#include "EtherFrame.h"
#include "IPassiveQueue.h"
#include "NotificationBoard.h"
#include "NotifierConsts.h"
#include "InterfaceEntry.h"

// TODO: refactor using a statemachine that is present in a single function
// TODO: this helps understanding what interactions are there and how they affect the state

Define_Module(EtherMACFullDuplex);

EtherMACFullDuplex::EtherMACFullDuplex()
{
}

void EtherMACFullDuplex::initialize(int stage)
{
    EtherMACBase::initialize(stage);

    if (stage == 0)
    {
        if (!par("duplexMode").boolValue())
            throw cRuntimeError("Half duplex operation is not supported by EtherMACFullDuplex, use the EtherMAC module for that! (Please enable csmacdSupport on EthernetInterface)");

        beginSendFrames();
    }
}

void EtherMACFullDuplex::initializeStatistics()
{
    EtherMACBase::initializeStatistics();

    // initialize statistics
    totalSuccessfulRxTime = 0.0;
}

void EtherMACFullDuplex::initializeFlags()
{
    EtherMACBase::initializeFlags();

    duplexMode = true;
    physInGate->setDeliverOnReceptionStart(false);
}

void EtherMACFullDuplex::handleMessage(cMessage *msg)
{
    if (!isOperational)
    {
        handleMessageWhenDown(msg);
        return;
    }

    if (channelsDiffer)
        readChannelParameters(true);

    if (msg->isSelfMessage())
        handleSelfMessage(msg);
    else if (msg->getArrivalGate() == upperLayerInGate)
        processFrameFromUpperLayer(check_and_cast<EtherFrame *>(msg));
    else if (msg->getArrivalGate() == physInGate)
        processMsgFromNetwork(check_and_cast<EtherTraffic *>(msg));
    else
        throw cRuntimeError("Message received from unknown gate!");

    if (ev.isGUI())
        updateDisplayString();
}

void EtherMACFullDuplex::handleSelfMessage(cMessage *msg)
{
    EV << "Self-message " << msg << " received\n";

    if (msg == endTxMsg)
        handleEndTxPeriod();
    else if (msg == endIFGMsg)
        handleEndIFGPeriod();
    else if (msg == endPauseMsg)
        handleEndPausePeriod();
    else
        throw cRuntimeError("Unknown self message received!");
}

void EtherMACFullDuplex::startFrameTransmission()
{
    ASSERT(curTxFrame);
    EV << "Transmitting a copy of frame " << curTxFrame << endl;

    EtherFrame *frame = curTxFrame->dup();  // note: we need to duplicate the frame because we emit a signal with it in endTxPeriod()

    if (frame->getSrc().isUnspecified())
        frame->setSrc(address);

    if (frame->getByteLength() < curEtherDescr->frameMinBytes)
        frame->setByteLength(curEtherDescr->frameMinBytes);

    // add preamble and SFD (Starting Frame Delimiter), then send out
    frame->addByteLength(PREAMBLE_BYTES+SFD_BYTES);

    // send
    EV << "Starting transmission of " << frame << endl;
    send(frame, physOutGate);

    scheduleAt(transmissionChannel->getTransmissionFinishTime(), endTxMsg);
    transmitState = TRANSMITTING_STATE;
}

void EtherMACFullDuplex::processFrameFromUpperLayer(EtherFrame *frame)
{
    if (frame->getByteLength() < MIN_ETHERNET_FRAME_BYTES)
        frame->setByteLength(MIN_ETHERNET_FRAME_BYTES);  // "padding"

    frame->setFrameByteLength(frame->getByteLength());

    EV << "Received frame from upper layer: " << frame << endl;

    emit(packetReceivedFromUpperSignal, frame);

    if (frame->getDest().equals(address))
    {
        error("logic error: frame %s from higher layer has local MAC address as dest (%s)",
                frame->getFullName(), frame->getDest().str().c_str());
    }

    if (frame->getByteLength() > MAX_ETHERNET_FRAME_BYTES)
    {
        error("packet from higher layer (%d bytes) exceeds maximum Ethernet frame size (%d)",
                (int)(frame->getByteLength()), MAX_ETHERNET_FRAME_BYTES);
    }

    if (!connected || disabled)
    {
        EV << (!connected ? "Interface is not connected" : "MAC is disabled") << " -- dropping packet " << frame << endl;
        emit(dropPkFromHLIfaceDownSignal, frame);
        numDroppedPkFromHLIfaceDown++;
        delete frame;

        requestNextFrameFromExtQueue();
        return;
    }

    // fill in src address if not set
    if (frame->getSrc().isUnspecified())
        frame->setSrc(address);

    bool isPauseFrame = (dynamic_cast<EtherPauseFrame*>(frame) != NULL);

    if (!isPauseFrame)
    {
        numFramesFromHL++;
        emit(rxPkFromHLSignal, frame);
    }

    if (txQueue.extQueue)
    {
        ASSERT(curTxFrame == NULL);
        curTxFrame = frame;
    }
    else
    {
        if (txQueue.innerQueue->isFull())
            error("txQueue length exceeds %d -- this is probably due to "
                  "a bogus app model generating excessive traffic "
                  "(or if this is normal, increase txQueueLimit!)",
                  txQueue.innerQueue->getQueueLimit());
        // store frame and possibly begin transmitting
        EV << "Frame " << frame << " arrived from higher layers, enqueueing\n";
        txQueue.innerQueue->insertFrame(frame);

        if (!curTxFrame && !txQueue.innerQueue->empty())
            curTxFrame = (EtherFrame*)txQueue.innerQueue->pop();
    }

    if (transmitState == TX_IDLE_STATE)
        startFrameTransmission();
}

void EtherMACFullDuplex::processMsgFromNetwork(EtherTraffic *msg)
{
    EV << "Received frame from network: " << msg << endl;

    // En este módulo la modificación consiste en la identificación del flujo generado de tráfico
    // Se obtiene la identificación del flujo y el tiempo de llegada el cual es enviado al nodo
    // appControl

    const char *mensaje = msg->getName();
    int tempo = 0;
    char config[15];

    cModule *parentModule = getParentModule();
    cModule *pparentModule = parentModule->getParentModule();
    const char *modulo = pparentModule->getName();
    cModule *targetModule = pparentModule->getSubmodule("appControl");
    cModule *red = pparentModule->getParentModule();
    char nombremoduloin[12];
    sprintf(nombremoduloin, "%s_ctc",mensaje);

    if (strncmp(modulo,"switch_1",8) == 0){
        if ((strcmp(mensaje,"vl_227")==0) || (strcmp(mensaje,"vl_218")==0) || (strcmp(mensaje,"vl_217")==0) ){

            cModule *nodo = red->getSubmodule("señalizador");
            cModule *owner = nodo->getSubmodule(mensaje);
            int tick = owner->par("sendWindowStart");
            tempo = tick + 5;
            sprintf(config, "%s %d",mensaje,tempo);
            sendDirect(new cMessage(config), targetModule, "direct");

            cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);
            moduloin->par("receive_window_start").setLongValue(tempo);
            moduloin->par("receive_window_end").setLongValue(tempo+5);
            moduloin->par("permanence_pit").setLongValue(tempo+5);

            cModule *moduloout = pparentModule->getSubmodule(mensaje);
            moduloout->par("sendWindowStart").setLongValue(tempo+6);
            moduloout->par("sendWindowEnd").setLongValue(tempo+7);

        }
        else if((strcmp(mensaje,"vl_219")==0) || (strcmp(mensaje,"vl_229")==0)){

            cModule *nodo = red->getSubmodule("alzavidrio_dd");
            cModule *owner = nodo->getSubmodule(mensaje);
            int tick = owner->par("sendWindowStart");
            tempo = tick + 5;

            cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);

            moduloin->par("receive_window_start").setLongValue(tempo);
            moduloin->par("receive_window_end").setLongValue(tempo+10);
            moduloin->par("permanence_pit").setLongValue(tempo+10);

            cModule *moduloout = pparentModule->getSubmodule(mensaje);
            moduloout->par("sendWindowStart").setLongValue(tempo+11);
            moduloout->par("sendWindowEnd").setLongValue(tempo+12);

        }
        else if((strcmp(mensaje,"vl_239")==0) || (strcmp(mensaje,"vl_249")==0)){
            cModule *nodo = red->getSubmodule("alzavidrio_di");
            cModule *owner = nodo->getSubmodule(mensaje);
            int tick = owner->par("sendWindowStart");
            tempo = tick + 5;

            cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);

            moduloin->par("receive_window_start").setLongValue(tempo);
            moduloin->par("receive_window_end").setLongValue(tempo+10);
            moduloin->par("permanence_pit").setLongValue(tempo+10);

            cModule *moduloout = pparentModule->getSubmodule(mensaje);
            moduloout->par("sendWindowStart").setLongValue(tempo+11);
            moduloout->par("sendWindowEnd").setLongValue(tempo+12);

        }
    }

    else if(strncmp(modulo,"switch_2",8) == 0 ){

        if(mensaje[5]=='0'){

            cModule *nodo = red->getSubmodule("contacto");
            cModule *owner = nodo->getSubmodule(mensaje);
            int tick = owner->par("sendWindowStart");
            tempo = tick + 5;
            sprintf(config, "%s %d",mensaje,tempo);

            cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);

            moduloin->par("receive_window_start").setLongValue(tempo);
            moduloin->par("receive_window_end").setLongValue(tempo+10);
            moduloin->par("permanence_pit").setLongValue(tempo+10);

            cModule *moduloout = pparentModule->getSubmodule(mensaje);
            moduloout->par("sendWindowStart").setLongValue(tempo+11);
            moduloout->par("sendWindowEnd").setLongValue(tempo+12);
            sendDirect(new cMessage(config), targetModule, "direct");

        }else if (mensaje[5]=='4'){

            cModule *nodo = red->getSubmodule("freno");
            cModule *owner = nodo->getSubmodule(mensaje);
            int tick = owner->par("sendWindowStart");
            tempo = tick + 5;
            sprintf(config, "%s %d",mensaje,tempo);
            sendDirect(new cMessage(config), targetModule, "direct");
            cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);

            int window_start = moduloin->par("receive_window_start");
            int window_end = moduloin->par("receive_window_end");
            int permanence_pit = moduloin->par("permanence_pit");

            moduloin->par("receive_window_start").setLongValue(tempo);
            moduloin->par("receive_window_end").setLongValue(tempo+10);
            moduloin->par("permanence_pit").setLongValue(tempo+10);

            cModule *moduloout = pparentModule->getSubmodule(mensaje);
            int sendWindowStart = moduloout->par("sendWindowStart");
            int sendWindowEnd = moduloout->par("sendWindowEnd");

            moduloout->par("sendWindowStart").setLongValue(tempo+11);
            moduloout->par("sendWindowEnd").setLongValue(tempo+12);


        }else if((strcmp(mensaje,"vl_211")==0)){

            cModule *nodo = red->getSubmodule("modulo_clima");
            cModule *owner = nodo->getSubmodule(mensaje);
            int tick = owner->par("sendWindowStart");
            tempo = tick + 5;

            cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);

            moduloin->par("receive_window_start").setLongValue(tempo);
            moduloin->par("receive_window_end").setLongValue(tempo+10);
            moduloin->par("permanence_pit").setLongValue(tempo+10);

            cModule *moduloout = pparentModule->getSubmodule(mensaje);
            moduloout->par("sendWindowStart").setLongValue(tempo+11);
            moduloout->par("sendWindowEnd").setLongValue(tempo+12);


        }else if(mensaje[5]=='3'){

            cModule *nodo = red->getSubmodule("velocimetro");
            cModule *owner = nodo->getSubmodule(mensaje);
            int tick = owner->par("sendWindowStart");
            tempo = tick + 5;

            cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);

            moduloin->par("receive_window_start").setLongValue(tempo);
            moduloin->par("receive_window_end").setLongValue(tempo+10);
            moduloin->par("permanence_pit").setLongValue(tempo+10);

            cModule *moduloout = pparentModule->getSubmodule(mensaje);
            moduloout->par("sendWindowStart").setLongValue(tempo+11);
            moduloout->par("sendWindowEnd").setLongValue(tempo+12);

        }else if(mensaje[5]=='2'){
            cModule *nodo = red->getSubmodule("acelerador");
            cModule *owner = nodo->getSubmodule(mensaje);
            int tick = owner->par("sendWindowStart");
            tempo = tick + 5;

            cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);

            moduloin->par("receive_window_start").setLongValue(tempo);
            moduloin->par("receive_window_end").setLongValue(tempo+10);
            moduloin->par("permanence_pit").setLongValue(tempo+10);

            cModule *moduloout = pparentModule->getSubmodule(mensaje);
            moduloout->par("sendWindowStart").setLongValue(tempo+11);
            moduloout->par("sendWindowEnd").setLongValue(tempo+12);

        }else if(mensaje[5]=='5'){

            cModule *nodo = red->getSubmodule("manubrio");
            cModule *owner = nodo->getSubmodule(mensaje);
            int tick = owner->par("sendWindowStart");
            tempo = tick + 5;

            cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);

            moduloin->par("receive_window_start").setLongValue(tempo);
            moduloin->par("receive_window_end").setLongValue(tempo+5);
            moduloin->par("permanence_pit").setLongValue(tempo+5);

            cModule *moduloout = pparentModule->getSubmodule(mensaje);
            moduloout->par("sendWindowStart").setLongValue(tempo+6);
            moduloout->par("sendWindowEnd").setLongValue(tempo+7);

        }else if(mensaje[5]=='6'){
            cModule *nodo = red->getSubmodule("transmision");
            cModule *owner = nodo->getSubmodule(mensaje);
            int tick = owner->par("sendWindowStart");
            tempo = tick + 5;

            cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);

            moduloin->par("receive_window_start").setLongValue(tempo);
            moduloin->par("receive_window_end").setLongValue(tempo+10);
            moduloin->par("permanence_pit").setLongValue(tempo+10);

            cModule *moduloout = pparentModule->getSubmodule(mensaje);
            moduloout->par("sendWindowStart").setLongValue(tempo+11);
            moduloout->par("sendWindowEnd").setLongValue(tempo+12);
        }

    }
    if (strncmp(modulo,"switch_3",8) == 0){
            if((strcmp(mensaje,"vl_219")==0) || (strcmp(mensaje,"vl_229")==0)){
                cModule *nodo = red->getSubmodule("alzavidrio_td");
                cModule *owner = nodo->getSubmodule(mensaje);
                int tick = owner->par("sendWindowStart");
                tempo = tick + 5;

                cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);

                moduloin->par("receive_window_start").setLongValue(tempo);
                moduloin->par("receive_window_end").setLongValue(tempo+10);
                moduloin->par("permanence_pit").setLongValue(tempo+10);

                cModule *moduloout = pparentModule->getSubmodule(mensaje);
                moduloout->par("sendWindowStart").setLongValue(tempo+11);
                moduloout->par("sendWindowEnd").setLongValue(tempo+12);

            }
            else if((strcmp(mensaje,"vl_239")==0) || (strcmp(mensaje,"vl_249")==0)){
                cModule *nodo = red->getSubmodule("alzavidrio_ti");
                cModule *owner = nodo->getSubmodule(mensaje);
                int tick = owner->par("sendWindowStart");
                tempo = tick + 5;

                cModule *moduloin = pparentModule->getSubmodule(nombremoduloin);

                moduloin->par("receive_window_start").setLongValue(tempo);
                moduloin->par("receive_window_end").setLongValue(tempo+10);
                moduloin->par("permanence_pit").setLongValue(tempo+10);

                cModule *moduloout = pparentModule->getSubmodule(mensaje);
                moduloout->par("sendWindowStart").setLongValue(tempo+11);
                moduloout->par("sendWindowEnd").setLongValue(tempo+12);

            }
        }

    if (!connected || disabled)
    {
        EV << (!connected ? "Interface is not connected" : "MAC is disabled") << " -- dropping msg " << msg << endl;
        if (dynamic_cast<EtherFrame *>(msg))    // do not count JAM and IFG packets
        {
            emit(dropPkIfaceDownSignal, msg);
            numDroppedIfaceDown++;
        }
        delete msg;

        return;
    }

    EtherFrame *frame = dynamic_cast<EtherFrame *>(msg);
    if (!frame)
    {
        if (dynamic_cast<EtherIFG *>(msg))
            throw cRuntimeError("There is no burst mode in full-duplex operation: EtherIFG is unexpected");
        check_and_cast<EtherFrame *>(msg);
    }

    totalSuccessfulRxTime += frame->getDuration();

    // bit errors
    if (frame->hasBitError())
    {
        numDroppedBitError++;
        emit(dropPkBitErrorSignal, frame);
        delete frame;
        return;
    }

    if (!dropFrameNotForUs(frame))
    {
        if (dynamic_cast<EtherPauseFrame*>(frame) != NULL)
        {
            int pauseUnits = ((EtherPauseFrame*)frame)->getPauseTime();
            delete frame;
            numPauseFramesRcvd++;
            emit(rxPausePkUnitsSignal, pauseUnits);
            processPauseCommand(pauseUnits);
        }
        else
        {
            processReceivedDataFrame((EtherFrame *)frame);
        }
    }
}

void EtherMACFullDuplex::handleEndIFGPeriod()
{
    if (transmitState != WAIT_IFG_STATE)
        error("Not in WAIT_IFG_STATE at the end of IFG period");

    // End of IFG period, okay to transmit
    EV << "IFG elapsed" << endl;

    beginSendFrames();
}

void EtherMACFullDuplex::handleEndTxPeriod()
{
    // we only get here if transmission has finished successfully
    if (transmitState != TRANSMITTING_STATE)
        error("End of transmission, and incorrect state detected");

    if (NULL == curTxFrame)
        error("Frame under transmission cannot be found");

    emit(packetSentToLowerSignal, curTxFrame);  //consider: emit with start time of frame

    if (dynamic_cast<EtherPauseFrame*>(curTxFrame) != NULL)
    {
        numPauseFramesSent++;
        emit(txPausePkUnitsSignal, ((EtherPauseFrame*)curTxFrame)->getPauseTime());
    }
    else
    {
        unsigned long curBytes = curTxFrame->getFrameByteLength();
        numFramesSent++;
        numBytesSent += curBytes;
        emit(txPkSignal, curTxFrame);
    }

    EV << "Transmission of " << curTxFrame << " successfully completed\n";
    delete curTxFrame;
    curTxFrame = NULL;
    lastTxFinishTime = simTime();
    getNextFrameFromQueue();

    if (pauseUnitsRequested > 0)
    {
        // if we received a PAUSE frame recently, go into PAUSE state
        EV << "Going to PAUSE mode for " << pauseUnitsRequested << " time units\n";

        scheduleEndPausePeriod(pauseUnitsRequested);
        pauseUnitsRequested = 0;
    }
    else
    {
        EV << "Start IFG period\n";
        scheduleEndIFGPeriod();
    }
}

void EtherMACFullDuplex::finish()
{
    EtherMACBase::finish();

    simtime_t t = simTime();
    simtime_t totalRxChannelIdleTime = t - totalSuccessfulRxTime;
    recordScalar("rx channel idle (%)", 100 * (totalRxChannelIdleTime / t));
    recordScalar("rx channel utilization (%)", 100 * (totalSuccessfulRxTime / t));
}

void EtherMACFullDuplex::handleEndPausePeriod()
{
    if (transmitState != PAUSE_STATE)
        error("End of PAUSE event occurred when not in PAUSE_STATE!");

    EV << "Pause finished, resuming transmissions\n";
    beginSendFrames();
}

void EtherMACFullDuplex::processReceivedDataFrame(EtherFrame *frame)
{
    emit(packetReceivedFromLowerSignal, frame);

    // strip physical layer overhead (preamble, SFD) from frame
    frame->setByteLength(frame->getFrameByteLength());

    // statistics
    unsigned long curBytes = frame->getByteLength();
    numFramesReceivedOK++;
    numBytesReceivedOK += curBytes;
    emit(rxPkOkSignal, frame);

    numFramesPassedToHL++;
    emit(packetSentToUpperSignal, frame);
    // pass up to upper layer
    send(frame, "upperLayerOut");
}

void EtherMACFullDuplex::processPauseCommand(int pauseUnits)
{
    if (transmitState == TX_IDLE_STATE)
    {
        EV << "PAUSE frame received, pausing for " << pauseUnitsRequested << " time units\n";
        if (pauseUnits > 0)
            scheduleEndPausePeriod(pauseUnits);
    }
    else if (transmitState == PAUSE_STATE)
    {
        EV << "PAUSE frame received, pausing for " << pauseUnitsRequested
           << " more time units from now\n";
        cancelEvent(endPauseMsg);

        if (pauseUnits > 0)
            scheduleEndPausePeriod(pauseUnits);
    }
    else
    {
        // transmitter busy -- wait until it finishes with current frame (endTx)
        // and then it'll go to PAUSE state
        EV << "PAUSE frame received, storing pause request\n";
        pauseUnitsRequested = pauseUnits;
    }
}

void EtherMACFullDuplex::scheduleEndIFGPeriod()
{
    transmitState = WAIT_IFG_STATE;
    simtime_t endIFGTime = simTime() + (INTERFRAME_GAP_BITS / curEtherDescr->txrate);
    scheduleAt(endIFGTime, endIFGMsg);
}

void EtherMACFullDuplex::scheduleEndPausePeriod(int pauseUnits)
{
    // length is interpreted as 512-bit-time units
    simtime_t pausePeriod = ((pauseUnits * PAUSE_UNIT_BITS) / curEtherDescr->txrate);
    scheduleAt(simTime() + pausePeriod, endPauseMsg);
    transmitState = PAUSE_STATE;
}

void EtherMACFullDuplex::beginSendFrames()
{
    if (curTxFrame)
    {
        // Other frames are queued, transmit next frame
        EV << "Transmit next frame in output queue\n";
        startFrameTransmission();
    }
    else
    {
        // No more frames set transmitter to idle
        transmitState = TX_IDLE_STATE;
        if (!txQueue.extQueue){
            // Output only for internal queue (we cannot be shure that there
            //are no other frames in external queue)
            EV << "No more frames to send, transmitter set to idle\n";
        }
    }
}

