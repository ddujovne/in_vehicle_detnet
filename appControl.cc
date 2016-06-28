#include <stdio.h>
#include <string.h>
#include <math.h>

#include "appControl.h"

#include "Ieee802Ctrl_m.h"
#include "NodeOperations.h"
#include "ModuleAccess.h"

Define_Module(appControl);

simsignal_t appControl::sentPkSignal = registerSignal("sentPk");
simsignal_t appControl::rcvdPkSignal = registerSignal("rcvdPk");

appControl::appControl()
{
    sendInterval = NULL;
    numPacketsPerBurst = NULL;
    packetLength = NULL;
    timerMsg = NULL;
    nodeStatus = NULL;
}

appControl::~appControl()
{
    cancelAndDelete(timerMsg);
}

void appControl::initialize(int stage)
{
    cSimpleModule::initialize(stage);

    if (stage == 0)
    {
        sendInterval = &par("sendInterval");
        numPacketsPerBurst = &par("numPacketsPerBurst");
        packetLength = &par("packetLength");
        etherType = par("etherType");

        seqNum = 0;
        WATCH(seqNum);

        // statistics
        packetsSent = packetsReceived = 0;
        WATCH(packetsSent);
        WATCH(packetsReceived);

        startTime = par("startTime");
        stopTime = par("stopTime");
        if (stopTime >= SIMTIME_ZERO && stopTime < startTime)
            error("Invalid startTime/stopTime parameters");
    }
}

void appControl::handleMessage(cMessage *msg)
{
    if (msg->isSelfMessage())
    {
        if (msg->getKind() == START)
        {
        }

    }
    else{

        // Se generan los paquetes de configuración que serán enviados al módulo gestor el cual los distribuirá
        // a los otros Switches

        std::string msgname = msg->getName();
        std::string mensaje = "configuracion " + msgname;
        const char * c = mensaje.c_str();

        cPacket *datapacket = new cPacket(c, IEEE802CTRL_DATA);
        long len = packetLength->longValue();
        datapacket->setByteLength(len);

        Ieee802Ctrl *etherctrl = new Ieee802Ctrl();
        etherctrl->setEtherType(etherType);
        etherctrl->setDest(destMACAddress);
        datapacket->setControlInfo(etherctrl);

        emit(sentPkSignal, datapacket);
        send(datapacket, "out");

    }
}

bool appControl::handleOperationStage(LifecycleOperation *operation, int stage, IDoneCallback *doneCallback)
{
    Enter_Method_Silent();
    if (dynamic_cast<NodeStartOperation *>(operation)) {
    }
    else if (dynamic_cast<NodeShutdownOperation *>(operation)) {

    }
    else if (dynamic_cast<NodeCrashOperation *>(operation)) {

    }
    else throw cRuntimeError("Unsupported lifecycle operation '%s'", operation->getClassName());
    return true;
}


void appControl::receivePacket(cPacket *msg)
{
    EV << "Received packet `" << msg->getName() << "'\n";

    packetsReceived++;
    emit(rcvdPkSignal, msg);
    delete msg;
}

void appControl::finish()
{
    cancelAndDelete(timerMsg);
    timerMsg = NULL;
}

