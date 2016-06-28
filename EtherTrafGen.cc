#include <stdio.h>
#include <string.h>
#include <math.h>

#include "EtherTrafGen.h"

#include "Ieee802Ctrl_m.h"
#include "NodeOperations.h"
#include "ModuleAccess.h"

Define_Module(EtherTrafGen);

simsignal_t EtherTrafGen::sentPkSignal = registerSignal("sentPk");
simsignal_t EtherTrafGen::rcvdPkSignal = registerSignal("rcvdPk");

EtherTrafGen::EtherTrafGen()
{
    sendInterval = NULL;
    numPacketsPerBurst = NULL;
    packetLength = NULL;
    timerMsg = NULL;
    nodeStatus = NULL;
}

EtherTrafGen::~EtherTrafGen()
{
    cancelAndDelete(timerMsg);
}

void EtherTrafGen::initialize(int stage)
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
    else if (stage == 3)
    {
        if (isGenerator())
            timerMsg = new cMessage("generateNextPacket");

        nodeStatus = dynamic_cast<NodeStatus *>(findContainingNode(this)->getSubmodule("status"));
        if (isNodeUp() && isGenerator())
            scheduleNextPacket(-1);
    }
}

void EtherTrafGen::handleMessage(cMessage *msg)
{
    if (!isNodeUp())
        throw cRuntimeError("Application is not running");
    if (msg->isSelfMessage())
    {
        if (msg->getKind() == START)
        {
            
        }

    }
    else{

         // Se obtiene la información proveniente del módulo appControl

         const char *mensaje = msg->getArrivalGate()->getFullName();
         const char *nombre = msg->getName();

         // Se identifica el Switch desde el cual se envía el mensaje

         if(strcmp(mensaje,"in[3]")==0){

            // Se envía la configuración al Switch 2 

             sendConfiguration(nombre,1);

             // Si corresponde al nodo señalizador se realiza un reajuste en la configuración recibida

             int tiempo=0;
             char name[40];
             char nombremoduloout[10];
             char nuevonombre[40];

            // Se envía la configuración al Switch 3 

             sscanf(msg->getName(), "%s %s %d", name, nombremoduloout, &tiempo);
             sprintf(nuevonombre, "%s %s %d", name, nombremoduloout, tiempo+21);
             sendConfiguration(nuevonombre,2);
         }

        // Si el Switch que envía la configuración es el número 2, se envía la configuración al Switch 1 y 3

         else if(strcmp(mensaje,"in[4]")==0){
             sendConfiguration(nombre,0);
             sendConfiguration(nombre,2);

         }
    }


}

bool EtherTrafGen::handleOperationStage(LifecycleOperation *operation, int stage, IDoneCallback *doneCallback)
{
    Enter_Method_Silent();
    if (dynamic_cast<NodeStartOperation *>(operation)) {
        if (stage == NodeStartOperation::STAGE_APPLICATION_LAYER && isGenerator())
            scheduleNextPacket(-1);
    }
    else if (dynamic_cast<NodeShutdownOperation *>(operation)) {
        if (stage == NodeShutdownOperation::STAGE_APPLICATION_LAYER)
            cancelNextPacket();
    }
    else if (dynamic_cast<NodeCrashOperation *>(operation)) {
        if (stage == NodeCrashOperation::STAGE_CRASH)
            cancelNextPacket();
    }
    else throw cRuntimeError("Unsupported lifecycle operation '%s'", operation->getClassName());
    return true;
}

bool EtherTrafGen::isNodeUp()
{
    return !nodeStatus || nodeStatus->getState() == NodeStatus::UP;
}

bool EtherTrafGen::isGenerator()
{
    return par("destAddress").stringValue()[0];
}


void EtherTrafGen::cancelNextPacket()
{
    cancelEvent(timerMsg);
}


void EtherTrafGen::sendConfiguration(const char *mensaje, int gate){

    // Se generan los paquetes que contienen la información de configuración

    EV << "Generating packet `" << mensaje << "'\n";

    cPacket *datapacket = new cPacket(mensaje, IEEE802CTRL_DATA);
    long len = packetLength->longValue();
    datapacket->setByteLength(len);

    Ieee802Ctrl *etherctrl = new Ieee802Ctrl();
    etherctrl->setEtherType(etherType);
    etherctrl->setDest(destMACAddress);
    datapacket->setControlInfo(etherctrl);

    emit(sentPkSignal, datapacket);
    send(datapacket, "out" , gate);
}

void EtherTrafGen::receivePacket(cPacket *msg)
{
    EV << "Received packet `" << msg->getName() << "'\n";

    packetsReceived++;
    emit(rcvdPkSignal, msg);
    delete msg;
}

void EtherTrafGen::finish()
{
    cancelAndDelete(timerMsg);
    timerMsg = NULL;
}

