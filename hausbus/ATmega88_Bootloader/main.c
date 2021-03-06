/*-----------------------------------------------------------------------------
*  Main.c
*/

/*-----------------------------------------------------------------------------
*  Includes
*/
#include <stdio.h>
#include <string.h>

#include <avr/io.h>
#include <avr/wdt.h>

#include "Types.h"
#include "Sio.h"
#include "Sysdef.h"
#include "Bus.h"
#include "Flash.h"


/*-----------------------------------------------------------------------------
*  ATMega88 fuse bit settings (internal RC oscillator)
*   
*  CKSEL0    = 0
*  CKSEL1    = 1
*  CKSEL2    = 0
*  CKSEL3    = 0
*  SUT0      = 0
*  SUT1      = 0
*  CKOUT     = 1
*  CKDIV8    = 0
*  BODLEVEL1 = 0
*  BODLEVEL2 = 0
*  BODLEVEL3 = 1
*  EESAVE    = 0
*  WDTON     = 1
*  DWEN      = 1
*  RSTDISBL  = 1
*  BOOTRST   = 0
*  BOOTSZ0   = 0
*  BOOTSZ1   = 0
*/ 
        
/*-----------------------------------------------------------------------------
*  Macros  
*/                    
/* eigene Adresse am Bus */
#define MY_ADDR    sMyAddr   

#define MODUL_ADDRESS       0
#define OSCCAL_CORR         1
                               
/* statemachine f�r Flashupdate */
#define WAIT_FOR_UPD_ENTER          0  
#define WAIT_FOR_UPD_ENTER_TIMEOUT  1  
#define WAIT_FOR_UPD_DATA           2            

/* max. Gr��e der Firmware */
#define MAX_FIRMWARE_SIZE    (6UL * 1024UL)   
#define FLASH_CHECKSUM       0x1234  /* Summe, die bei Checksum herauskommen muss */
#define CHECKSUM_BLOCK_SIZE  256 /* words */

/*-----------------------------------------------------------------------------
*  Typedefs
*/  

/*-----------------------------------------------------------------------------
*  Variables
*/  
volatile UINT8 gTimeS8 = 0;

static TBusTelegramm  *spRxBusMsg;
static TBusTelegramm  sTxBusMsg;
static UINT8 sFwuState = WAIT_FOR_UPD_ENTER_TIMEOUT;
static UINT8 sMyAddr;


/*-----------------------------------------------------------------------------
*  Functions
*/
extern void ApplicationEntry(void);

static void ProcessBus(UINT8 ret);      
static void SetMsg(TBusMsgType type, UINT8 receiver);

/*-----------------------------------------------------------------------------
*  program start
*/
int main(void) {

   UINT8   ret;  
   UINT16  flashWordAddr;    
   UINT16  sum;

   cli();
   MCUSR = 0;
   wdt_disable();

   /* get oscillator correction value from EEPROM */
   EEAR = OSCCAL_CORR;
   /* Start eeprom read by writing EERE */
   EECR |= (1 << EERE);
   /* read data */
   OSCCAL += EEDR;

   /* get modul adress from EEPROM */
   EEAR = MODUL_ADDRESS;
   /* Start eeprom read by writing EERE */
   EECR |= (1 << EERE);
   /* read data */
   sMyAddr = EEDR;

   /* configure pins to input with pull up */
   PORTB = 0b11111111;
   DDRB =  0b00000000;

   PORTC = 0b11111111;
   DDRC =  0b00000000;

   PORTD = 0b11111111;
   DDRD =  0b00100010;

   /* configure Timer 0 */
   /* prescaler clk/64 -> Interrupt period 256/1000000 * 64 = 16.384 ms */
   TCCR0B = 3 << CS00; 
   TIMSK0 = 1 << TOIE0;

   SioInit();
   spRxBusMsg = BusMsgBufGet();

   /* Enable change of Interrupt Vectors */
   MCUCR = (1 << IVCE);
   /* Move interrupts to Boot Flash section */
   MCUCR = (1 << IVSEL);

   /* Pr�fsumme der Applikation berechnen */
   sum = 0;
   for (flashWordAddr = 0; flashWordAddr < (MAX_FIRMWARE_SIZE / 2); flashWordAddr += CHECKSUM_BLOCK_SIZE) {
      sum += FlashSum(flashWordAddr, (UINT8)CHECKSUM_BLOCK_SIZE);
   }

   if (sum != FLASH_CHECKSUM) {
      /* Fehler */
      sFwuState = WAIT_FOR_UPD_ENTER;      
   }
   sei();
      
   /* Startup-Msg senden */
   sTxBusMsg.type = eBusDevStartup;  
   sTxBusMsg.senderAddr = MY_ADDR; 
   BusSend(&sTxBusMsg);  
   SioReadFlush();
 
   /* Hauptschleife */
   while (1) {   
      ret = BusCheck();
      ProcessBus(ret);
      /* Mit timeout auf Request zum Firmwareupdate warten  */
      if (sFwuState == WAIT_FOR_UPD_ENTER_TIMEOUT) {
         if (gTimeS8 >= 4) {
            /* Application starten */
            break;
         }
      } 
   } 
            
   cli();
   
   /* Enable change of Interrupt Vectors */
   MCUCR = (1 << IVCE);
   /* Move interrupts to application section */
   MCUCR = (0 << IVSEL);

   /* jump to application */
   ApplicationEntry();
   
   /* never reach this */
   return 0;
} 

/*-----------------------------------------------------------------------------
*  Verarbeitung der Bustelegramme
*/
static void ProcessBus(UINT8 ret) {
   
   TBusMsgType   msgType;    
   UINT16        *pData;
   UINT16        wordAddr;
   BOOL          rc;
   BOOL          msgForMe = FALSE;

   if (ret == BUS_MSG_OK) {
      msgType = spRxBusMsg->type; 
      switch (msgType) {  
         case eBusDevReqReboot:
         case eBusDevReqUpdEnter:
         case eBusDevReqUpdData:
         case eBusDevReqUpdTerm:
            if (spRxBusMsg->msg.devBus.receiverAddr == MY_ADDR) {
               msgForMe = TRUE;
            }
         default:
            break;
      }
      if (msgForMe == FALSE) {
         return;
      }

    
      if (msgType == eBusDevReqReboot) {
         /* �ber Watchdog Reset ausl�sen */    
         /* Watchdogtimeout auf kurzeste Zeit (14 ms) stellen */                     
         cli();
         wdt_enable(WDTO_15MS);
         /* warten auf Reset */
         while (1);
      } else {
         switch (sFwuState) {
            case WAIT_FOR_UPD_ENTER_TIMEOUT:
            case WAIT_FOR_UPD_ENTER:
               if (msgType == eBusDevReqUpdEnter) {
                  /* Applicationbereich des Flash l�schen */  
                  FlashErase();
                  /* Antwort senden */
                  SetMsg(eBusDevRespUpdEnter, spRxBusMsg->senderAddr);
                  BusSend(&sTxBusMsg);
                  sFwuState = WAIT_FOR_UPD_DATA;     
               }           
               break;
            case WAIT_FOR_UPD_DATA:
               if (msgType == eBusDevReqUpdData) {
                  wordAddr = spRxBusMsg->msg.devBus.x.devReq.updData.wordAddr;
                  pData = spRxBusMsg->msg.devBus.x.devReq.updData.data;
                  /* Flash programmieren */
                  rc = FlashProgram(wordAddr, pData, sizeof(spRxBusMsg->msg.devBus.x.devReq.updData.data) / 2);
                  /* Antwort senden */
                  SetMsg(eBusDevRespUpdData, spRxBusMsg->senderAddr);
                  if (rc == TRUE) {
                     /* Falls Programmierung des Block OK: empfangene wordAddr zur�cksenden */
                     sTxBusMsg.msg.devBus.x.devResp.updData.wordAddr = wordAddr;
                  } else {
                     /* Problem bei Programmierung: -1 als wordAddr zur�cksenden */
                     sTxBusMsg.msg.devBus.x.devResp.updData.wordAddr = -1;
                  }
                  BusSend(&sTxBusMsg);  
               } else if (msgType == eBusDevReqUpdTerm) {
                  /* programmiervorgang im Flash abschlie�en (falls erforderlich) */
                  rc = FlashProgramTerminate();
                  /* Antwort senden */
                  SetMsg(eBusDevRespUpdTerm, spRxBusMsg->senderAddr);
                  if (rc == TRUE) {
                     /* Falls Programmierung OK: success auf 1 setzen */
                     sTxBusMsg.msg.devBus.x.devResp.updTerm.success = 1;
                  } else {
                     /* Problem bei Programmierung: -1 als wordAddr zur�cksenden */
                     sTxBusMsg.msg.devBus.x.devResp.updTerm.success = 0;
                  }
                  BusSend(&sTxBusMsg);
               }
               break;
            default:
               break;
         }
      }
   }
}

/*-----------------------------------------------------------------------------
*  Sendedaten eintragen
*/
static void SetMsg(TBusMsgType type, UINT8 receiver) {
   sTxBusMsg.type = type;  
   sTxBusMsg.senderAddr = MY_ADDR; 
   sTxBusMsg.msg.devBus.receiverAddr = receiver;
}

/*-----------------------------------------------------------------------------
*  Timer 0 overflow ISR
*  period:  16.384 ms
*/
ISR(TIMER0_OVF_vect) {

  static UINT8 intCnt = 0;

  intCnt++;
  if (intCnt >= 61) { /* 16.384 ms * 61 = 1 s*/
     intCnt = 0;
     gTimeS8++;
  }
}

