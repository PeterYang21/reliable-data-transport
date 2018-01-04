/*
 * FILE: rdt_receiver.cc
 * DESCRIPTION: Reliable data transfer receiver.
 *
 * | payload size |<-             payload             ->|
 * The first byte of each packet indicates the size of the payload
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdt_struct.h"
#include "rdt_receiver.h"

namespace receiver {
    int expect_seqnum = 0;
    int ack = 0;
    
    int header_size = 6;
    
    int chksum_index1 = 0;
    int chksum_index2 = 1;
    int len_index = 2;
    int seq_indexH = 3;
    int seq_indexM = 4;
    int seq_indexL = 5;
}

namespace seq {
    uint8_t seq_high;
    uint8_t seq_med;
    uint8_t seq_low;
}

using namespace receiver;
using namespace seq;

unsigned short int Get_Sum(char* data){

    int len = RDT_PKTSIZE-2; // sum all bits except check sum field
    unsigned int chksum = 0; // 32 bits
    while(len > 1){
        chksum += *(((unsigned short int*)data++)); // 16 bits
        len -= 2;
    }
    if(len == 1){ // if length is odd
        chksum += *(unsigned char*) data;
    }
    while(chksum >> 16){
        chksum = (chksum >> 16)+(chksum & 0xffff);
    }
    return (unsigned short int)chksum;
}

void Get_Seq(int seq){
    if(seq < 256){ // 0 -> 255
        seq_high = 0;
        seq_med = 0;
        seq_low = seq;
    }
    else if(seq > 255 && seq < 65536){ // 256 -> 65535
        seq_high = 0;
        seq_med = seq / 256;
        seq_low = seq % 256;
    }
    else if(seq > 65535){ // 65536 -> larger
        seq_high = seq / 65536;
        int rest = seq % 65536;
        if(rest > 255 && rest < 65536){
            seq_med = rest / 256;
            seq_low = rest % 256;
        }
        else if(rest < 256){
            seq_med = 0;
            seq_low = rest;
        }
    }
}

/* receiver initialization, called once at the very beginning */
void Receiver_Init()
{
    fprintf(stdout, "At %.2fs: receiver initializing ...\n", GetSimulationTime());
}

/* receiver finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to use this opportunity to release some 
   memory you allocated in Receiver_init(). */
void Receiver_Final()
{
    fprintf(stdout, "At %.2fs: receiver finalizing ...\n", GetSimulationTime());
}

/* event handler, called when a packet is passed from the lower layer at the 
   receiver */
void Receiver_FromLowerLayer(struct packet *pkt)
{
//    printf("\nReceived SEQ-NUM: %d Expected SEQ-NUM: %d\n", pkt->seqnum, expect_seqnum);
    
    int corrupt = 1;
    unsigned short int curr_sum = Get_Sum(pkt->data+2);
    // printf("Curr-sum: %x\n", curr_sum);
    unsigned short int curr_low = curr_sum & 0xff;
    // printf("Curr-low: %x\n", curr_low);
    unsigned short int curr_high = ((curr_sum >> 8) & 0xff);
    // printf("Curr-high: %x\n", curr_high);
    unsigned short int orig_low = pkt->data[chksum_index2] & 0xff;
    // printf("Orig-low: %x\n", orig_low);
    unsigned short int orig_high = pkt->data[chksum_index1] & 0xff;
    // printf("Orig-high: %x\n", orig_high);
    
    if((curr_low ^ orig_low) == 0xff && (curr_high ^ orig_high) == 0xff){
        corrupt = 0;
    }
    uint8_t tmp1 = pkt->data[seq_indexL];
    uint8_t tmp2 = pkt->data[seq_indexM];
    uint8_t tmp3 = pkt->data[seq_indexH];
    if(pkt->data[seq_indexL] < 0)
        tmp1 = 256 - (0-pkt->data[seq_indexL]);
    if(pkt->data[seq_indexM] < 0)
        tmp2 = 256 - (0-pkt->data[seq_indexM]);
    if(pkt->data[seq_indexH] < 0)
        tmp3 = 256 - (0-pkt->data[seq_indexH]);

    int seq_decimal = 65536 * tmp3 + 256 * tmp2 + tmp1;

    // printf("Expected: %d, Received: %d\n", expect_seqnum, seq_decimal);
    
    if(seq_decimal == expect_seqnum && !corrupt){
        /* construct a message and deliver to the upper layer */
        struct message *msg = (struct message*) malloc(sizeof(struct message));
        ASSERT(msg!=NULL);
        
        msg->size = pkt->data[2];
        
        /* sanity check in case the packet is corrupted */
        if (msg->size<0) msg->size=0;
        if (msg->size>RDT_PKTSIZE-header_size) msg->size=RDT_PKTSIZE-header_size;
        
        msg->data = (char*) malloc(msg->size);
        ASSERT(msg->data!=NULL);
        memcpy(msg->data, pkt->data+header_size, msg->size); /////
        Receiver_ToUpperLayer(msg); // sent to upper layer
        
        struct packet *sndpacket = (struct packet*) malloc(sizeof(struct packet));
        
        Get_Seq(expect_seqnum);
        sndpacket->data[seq_indexH] = seq_high;
        sndpacket->data[seq_indexM] = seq_med;
        sndpacket->data[seq_indexL] = seq_low;
    
        sndpacket->ack = ack; // modify
        Receiver_ToLowerLayer(sndpacket);
        
        // printf("#Receiver_FromLowerLayer: expect_seqnum: %d, recv-pkt-seq: %d\n", expect_seqnum, seq_decimal);
        // printf("#Receiver_FromLowerLayer: ACK: %d\n", ack);
        
        expect_seqnum++;
        ack = expect_seqnum; 
        
        /* don't forget to free the space */
        if (msg->data!=NULL) free(msg->data);
        if (msg!=NULL) free(msg);
    }
    else{ // packet loss occurs 
        struct packet *sndpacket = (struct packet*) malloc(sizeof(struct packet));
        if(expect_seqnum == 0){ // case: pkt0 is lost
            sndpacket->ack = -1;
        }
        else{
            sndpacket->ack = expect_seqnum-1;
        }
//        printf("\nPacket loss occurs\n");
        // timer for ack
        Receiver_ToLowerLayer(sndpacket);
    }
}
