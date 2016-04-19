#include "kernel.h"

pcb_t pcb[ 16 ], *current = NULL;

void scheduler( ctx_t* ctx ) {
    //Go to next pcb

    int currentProgram = current->pid;
    int next = nextProgram();

    memcpy( &pcb[ currentProgram ].ctx, ctx, sizeof( ctx_t ) );
    memcpy( ctx, &pcb[ next ].ctx, sizeof( ctx_t ) );


    current = &pcb[ next ];
    printNum(next);


}

//Program to find the next program available
int nextProgram (){
    int currentProgram = current->pid;
    for (int i = currentProgram + 1; i%16 != currentProgram; i++) {
        if (pcb[i%16].ctx.sp != 0) {
            return i%16;
        }
    }
    return currentProgram;

}

void kernel_handler_rst(ctx_t* ctx) {
    numPrograms = 0;

    for (int i = 0; i < 16; i++) {
        memset( &pcb[ i ], 0, sizeof( pcb_t ) );
    }

    pcb[ 0 ].pid      = 0;
    pcb[ 0 ].ctx.cpsr = 0x50;
    pcb[ 0 ].ctx.pc   = ( uint32_t )( entry_P0 );
    pcb[ 0 ].ctx.sp   = ( uint32_t )(  &tos_Programs );
    numPrograms ++;

    pcb[ 1 ].pid      = 1;
    pcb[ 1 ].ctx.cpsr = 0x50;
    pcb[ 1 ].ctx.pc   = ( uint32_t )( entry_P1 );
    pcb[ 1 ].ctx.sp   = ( uint32_t )(  &tos_Programs - ( 0x00001000 ));
    numPrograms ++;


    pcb[ 2 ].pid      = 2;
    pcb[ 2 ].ctx.cpsr = 0x50;
    pcb[ 2 ].ctx.pc   = ( uint32_t )( entry_P2 );
    pcb[ 2 ].ctx.sp   = ( uint32_t )(  &tos_Programs - ( 0x00002000 ));
    numPrograms ++;

    pcb[ 3 ].pid      = 3;
    pcb[ 3 ].ctx.cpsr = 0x50;
    pcb[ 3 ].ctx.pc   = ( uint32_t )( entry_P1 );
    pcb[ 3 ].ctx.sp   = 0;
    numPrograms ++;



    current = &pcb[ 0 ];
    memcpy( ctx, &current->ctx, sizeof( ctx_t ) );


  /* Configure the mechanism for interrupt handling by
   *
   * - configuring timer st. it raises a (periodic) interrupt for each
   *   timer tick,
   * - configuring GIC st. the selected interrupts are forwarded to the
   *   processor via the IRQ interrupt signal, then
   * - enabling IRQ interrupts.
   */

   UART0->IMSC           |= 0x00000010; // enable UART    (Rx) interrupt
   UART0->CR              = 0x00000301; // enable UART (Tx+Rx)

   TIMER0->Timer1Load     = 0x00100000; // select period = 2^20 ticks ~= 1 sec
   TIMER0->Timer1Ctrl     = 0x00000002; // select 32-bit   timer
   TIMER0->Timer1Ctrl    |= 0x00000040; // select periodic timer
   TIMER0->Timer1Ctrl    |= 0x00000020; // enable          timer interrupt
   TIMER0->Timer1Ctrl    |= 0x00000080; // enable          timer

   GICC0->PMR             = 0x000000F0; // unmask all            interrupts
   GICD0->ISENABLER[ 1 ] |= 0x00000010; // enable timer          interrupt
   GICC0->CTLR            = 0x00000001; // enable GIC interface
   GICD0->CTLR            = 0x00000001; // enable GIC distributor

   irq_enable();
   writeStr("reset\n");
   return;
}

void kernel_handler_svc( ctx_t* ctx, uint32_t id ) {
  /* Based on the identified encoded as an immediate operand in the
   * instruction,
   *
   * - read  the arguments from preserved usr mode registers,
   * - perform whatever is appropriate for this system call,
   * - write any return value back to preserved usr mode registers.
   */


// write( fd, x, n )
    switch( id ) {
        case 0x00 : { // yield()
            scheduler( ctx );
            break;
        }
        case 0x01 : { // write( fd, x, n )
            int   fd = ( int   )( ctx->gpr[ 0 ] );
            char*  x = ( char* )( ctx->gpr[ 1 ] );
            int    n = ( int   )( ctx->gpr[ 2 ] );

            for( int i = 0; i < n; i++ ) {
                PL011_putc( UART0, *x++ );
            }

            ctx->gpr[ 0 ] = n;
            break;
        }
        case 0x02 : {
            forkProgram(ctx);
        }
        case 0x03 : {
            exitProgram(ctx);
        }
        default   : { // unknown
            break;
        }
    }
}

void kernel_handler_irq(ctx_t* ctx) {
    //  read  the interrupt identifier so we know the source.

    uint32_t id = GICC0->IAR;

    // Step 4: handle the interrupt, then clear (or reset) the source.

    if( id == GIC_SOURCE_TIMER0 ) {
        TIMER0->Timer1IntClr = 0x01;
        scheduler(ctx);
    }

    if( id == GIC_SOURCE_UART0 ) {
      uint8_t x = PL011_getc( UART0 );

      switch (x) {
          case 'f': fork();
          case 'e': exit();
      }

      UART0->ICR = 0x10;
    }

    // Step 5: write the interrupt identifier to signal we're done.

    GICC0->EOIR = id;
    return;
}

void forkProgram(ctx_t* ctx){
    if (numPrograms < 16) {
        writeStr("Fork Start\n");
        //Get unique ID
        int newID = numPrograms;

        // Calculate top of stack of parent
        uint32_t tos_Parent = (uint32_t) tos_Programs - ( current->pid * ( 0x00001000 ));

        //Calculate the top of the stack of child
        uint32_t tos_Child = ( uint32_t ) tos_Programs - ( newID * ( 0x00001000 ));

        //Calculate the position of the stack pointer of child process
        uint32_t offset = (uint32_t) tos_Parent - (current->ctx.sp);
        uint32_t sp_Child = (uint32_t) tos_Child - offset;
        //printf("%s\n",offset );

        //Increment number of programs
        numPrograms++;

        //Set new programs properties
        pcb[ newID ].pid      = newID;

        //Copy context
        memcpy(&pcb[newID].ctx, ctx, sizeof(ctx_t) );

        //Copy stack - 0x0..0 because it copys from the bottem of the stack,
        memcpy(tos_Child - 0x00001000, tos_Parent - 0x00001000, 0x00001000 );

        //Set the new stack pointer
        pcb[ newID ].ctx.sp   = sp_Child;

        writeStr("Fork\n");
        return;
    } else {
        writeStr("Memory Full\n");
        return;
    }
}

void exitProgram(ctx_t* ctx){
    memset( &current, 0, sizeof( pcb_t ) );

    int next = nextProgram();
    memcpy( ctx, &pcb[ next ].ctx, sizeof( ctx_t ) );

    current = &pcb[ next ];
    printNum(next);

    return;
}

void printNum(int num) {
    char buffer[2];
    itoa(num,buffer);
    for (int j=0; j<2;j++){
        PL011_putc( UART0, buffer[j]);
    }
    return;
}


//Things to do
//Assign top of stack, change creation of programs to be dynamic? meaning that the Programs,p1,p2 are created with functions
