 A &= 0x1FFF;

 unsigned val = ((A & 0x1C00) >> 8) | (A & 0x3);

 switch(val)
 {
   case 0:
     goto VDC_00;
   case 1:
     goto VDC_01;
   case 2:
     goto VDC_02;
   case 3:
     goto VDC_03;
   case 4:
     goto VCE_00;
   case 5:
     goto VCE_01;
   case 6:
     goto VCE_02;
   case 7:
     goto VCE_03;
   case 8:
   case 9:
   case 10:
   case 11:
     goto PSG;
   case 12:
     goto TIMER_00;
   case 13:
     goto TIMER_01;
   case 14:
     goto TIMER_02;
   case 15:
     goto TIMER_03;
   case 16:
     goto INPUT_00;
   case 17:
     goto INPUT_01;
   case 18:
     goto INPUT_02;
   case 19:
     goto INPUT_03;
   case 20:
     goto IRQ_00;
   case 21:
     goto IRQ_01;
   case 22:
     goto IRQ_02;
   case 23:
     goto IRQ_03;
   case 24:
   case 25:
   case 26:
   case 27:
     goto CDROM;
   case 28:
   case 29:
   case 30:
   case 31:
     goto EXP;
 }

#if IOREAD_SGX==1
  VDC_00:
  VDC_01:
  VDC_02:
  VDC_03: HuC6280_StealCycle();
	  return(VDC_Read(A&0x1F, TRUE));
#else

 VDC_00:
        HuC6280_StealCycle();
        return(VDC_Read(0, FALSE));

 VDC_01:
        HuC6280_StealCycle();
        return(VDC_Read(1, FALSE));

 VDC_02:
        HuC6280_StealCycle();
        return(VDC_Read(2, FALSE));

 VDC_03:
        HuC6280_StealCycle();
        return(VDC_Read(3, FALSE));
#endif

 VCE_00:
 VCE_01:
 VCE_02:
 VCE_03:
        HuC6280_StealCycle();
        return(VCE_Read(A));

 PSG:
        if(HuCPU.in_block_move)
         return(0);
        return(PCEIODataBuffer);


 TIMER_00:
 TIMER_01:
 TIMER_02:
 TIMER_03:

        if(HuCPU.in_block_move)
         return(0);
        {
         uint8 ret = HuC6280_TimerRead(A);
         PCEIODataBuffer = ret;
         return(ret);
        }

 INPUT_00:
 INPUT_01:
 INPUT_02:
 INPUT_03:
               if(HuCPU.in_block_move)
                return(0);
               {
                uint8 ret = INPUT_Read(A, 0);
                PCEIODataBuffer = ret;
                return(ret);
               }

 IRQ_00:
 IRQ_01:
 IRQ_02:
 IRQ_03:
               if(HuCPU.in_block_move)
                return(0);
               {
                uint8 ret = HuC6280_IRQStatusRead(A);
                PCEIODataBuffer = ret;
                return(ret);
               }

 CDROM:
               if(IsTsushin)
                return(PCE_TsushinRead(A));

               if(!PCE_IsCD)
                return(0xFF);

               if((A & 0x1E00) == 0x1A00)
               {
                if(arcade_card)
                 return(arcade_card->Read(A & 0x1FFF));
                else
                 return(0);
               }
               else
	       {
		int32 next_cd_event;

                return(PCECD_Read(HuCPU.timestamp * 3, A, next_cd_event));
	       }


 EXP:
 //printf("Meow: %08x, %02x:%04x\n", A, A >> 13, A & 0x1FFF);
 return(0xFF);
