/*
 * FILE: rdt_sender.cc
 * DESCRIPTION: Reliable data transfer sender.
 *       
 *| payload size |<-             payload             ->|
 *The first byte of each packet indicates the size of the payload
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "rdt_struct.h"
#include "rdt_sender.h"

#define TIMEOUT 0.3
#define WINDOW_SIZE 10
#define BUFFER_SIZE 60000

namespace sender{
    int buf_index = 0; // indicate next empty buffer slot
    int start_index = 0; // indicate next index for loading data from buffer
    
    int base = 0;
    int next_seqnum = 0;
    
    int header_size = 6; // non-payload size
    char buffer [BUFFER_SIZE][RDT_PKTSIZE];
    
    int chksum_index1 = 0; // starting index of checksum field
    int chksum_index2 = 1;
    int len_index = 2;
    int seq_indexH = 3;
    int seq_indexM = 4;
    int seq_indexL = 5;
}

namespace sequence {
    uint8_t seq_high;
    uint8_t seq_med;
    uint8_t seq_low;
}

using namespace sender;
using namespace sequence;

unsigned short int Get_Checksum(char *data){
    
  int len = RDT_PKTSIZE-2; // sum all bits except check sum field
  unsigned int chksum = 0; // 32 bits
  while(len > 1){
    chksum += *(((unsigned short int*)data++)); // 16 bits
    len -= 2;
  }
  if(len > 0){ // if length is odd
    chksum += *((unsigned char*) data); // 8 bits
  }
  while(chksum >> 16){
    chksum = (chksum >> 16)+(chksum & 0xffff);
  }
  unsigned short int sum = ~chksum;
  return sum;
}

/* check if corruption occurs */
int Check_Corruption(struct packet *pkt){
    
    int corrupt = 1;
    unsigned short int curr_sum = Get_Checksum(pkt->data+2);
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
    
    return corrupt;
}

void Get_Seqnum(unsigned int seq){
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

/* Sender sends packet to receiver by first sending it to lower layer */
void Sender_SendPacket(){
    struct packet pkt;
    
    while(next_seqnum < base + WINDOW_SIZE){ // within window size
        if(start_index < buf_index){
            memset(pkt.data, 0, sizeof(pkt.data));
            
            pkt.data[2] = strlen(buffer[start_index]);
            memcpy(pkt.data+header_size, buffer[start_index], pkt.data[2]);
            // printf("Start-Index: %d, Buffer-Index: %d\n",start_index,buf_index);
            start_index++;
            
            Get_Seqnum(next_seqnum);

            /* insert sequence number */
            pkt.data[seq_indexH] = seq_high;
            pkt.data[seq_indexM] = seq_med;
            pkt.data[seq_indexL] = seq_low;

            /* insert checksum */
            unsigned short int sum = (Get_Checksum(pkt.data+2));
            // printf("CHECK-sum: %x\n", sum);
            char low = (char)sum & 0xff;
            char high = (char)((sum >> 8) & 0xff);
            pkt.data[chksum_index1] = high;
            pkt.data[chksum_index2] = low;
            
            /* send it out through the lower layer */
            Sender_ToLowerLayer(&pkt);
            
            // printf("\nBase: %d\n",base);
            // printf("Sent-SQN: %d\n", next_seqnum);
            
            if(base == next_seqnum){
                Sender_StartTimer(TIMEOUT);
            }
            
            next_seqnum++; // increment
        }
        else {
            break;
        }
    }
}

/* sender initialization, called once at the very beginning */
void Sender_Init()
{
    fprintf(stdout, "At %.2fs: sender initializing ...\n", GetSimulationTime());
}

/* sender finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to take this opportunity to release some 
   memory you allocated in Sender_init(). */
void Sender_Final()
{
    fprintf(stdout, "At %.2fs: sender finalizing ...\n", GetSimulationTime());
}

/* event handler, called when a message is passed from the upper layer at the 
   sender */
void Sender_FromUpperLayer(struct message *msg)
{
    /* maximum payload size */
    int maxpayload_size = RDT_PKTSIZE - header_size;

//    memset(pkt.data, 0, sizeof(pkt.data));

    /* the cursor always points to the first unsent byte in the message */
    int cursor = 0;

    while(msg->size - cursor > maxpayload_size){
      memcpy(buffer[buf_index], msg->data+cursor, maxpayload_size);
      buf_index++;
      cursor += maxpayload_size;
    }
    if(msg->size > cursor){
      int len = msg->size - cursor;
      memcpy(buffer[buf_index], msg->data+cursor, len);
      buf_index++;
        // printf("BUFFER-Index: %d\n", buf_index);
    }
	
    Sender_SendPacket();
}

/* event handler, called when a packet is passed from the lower layer at the 
   sender */
void Sender_FromLowerLayer(struct packet *pkt)
{
  /* check if corruption occurs */
  int corrupt = Check_Corruption(pkt);
    
  if(!corrupt){

      base = pkt->ack + 1;
      if(base == next_seqnum){
          /* if there is still data unsent in sender side's buffer,
             load them into packets and continue sending */
          if(start_index != buf_index){
              Sender_SendPacket();
          }
          else{ /* no data left in buffer and base reaching next_seqnum
                   indicate the whole transferring process terminates */
              // printf("EQUALS!!! %d %d\n",start_index, buf_index);
              Sender_StopTimer();
          }
      }
      else{ // start timer for the updated oldest unacked pkt
          Sender_StartTimer(TIMEOUT);
          // printf("\nStart timer for new base: %f\n", GetSimulationTime());
      }
  }
}

/* event handler, called when the timer expires */
void Sender_Timeout()
{
  int tmp = base;
  Sender_StartTimer(TIMEOUT);
  // printf("\nTIMEOUT BASE IS : %d\n",tmp);
  // printf("Start Resending. Resend till Seq: %d\n",next_seqnum-1);

    while(tmp < next_seqnum){
        packet pkt;
        pkt.data[2] = strlen(buffer[tmp]);
        memcpy(pkt.data+header_size, buffer[tmp], pkt.data[2]);
	   
        Get_Seqnum(tmp);
        pkt.data[seq_indexH] = seq_high;
        pkt.data[seq_indexM] = seq_med;
        pkt.data[seq_indexL] = seq_low;
        
        /* insert checksum */
        unsigned short int sum = (Get_Checksum(pkt.data+2));
        // printf("CHECK-sum: %x\n", sum);
        char low = (char)sum & 0xff;
        char high = (char)((sum >> 8) & 0xff);
        pkt.data[chksum_index1] = high;
        pkt.data[chksum_index2] = low;
        
        Sender_ToLowerLayer(&pkt); // re-sent packet can also be lost
        tmp++;
    }
}
