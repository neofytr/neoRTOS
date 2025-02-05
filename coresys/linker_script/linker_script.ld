ENTRY(reset_handler)

MEMORY 
{
    /* FLASH is a read-only memory (ROM) and can only be initialized while flashing; and cannot be
    modified by a running program; a running program can only modify the SRAM */
    FLASH(rx): ORIGIN = 0x08000000, LENGTH = 512K
    SRAM(rwx): ORIGIN = 0x20000000, LENGTH = 96K
}

/* Define stack and heap sizes */
__max_heap_size = 0x400;  /* 1KB heap */
__max_stack_size = 0x200; /* 512B stack */

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))   
        . = ALIGN(4);
    } > FLASH

    .text : 
    {
        . = ALIGN(4);
        *(.text)           
        *(.text*)          
        *(.rodata)         
        *(.rodata*)        
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* We place the read only data section in flash since it's Read-Only Memory */

    _sidata = LOADADDR(.data);

    /* _sidata now contains the start address of where the .data section is loaded in flash memory */

    .data :
    {
        . = ALIGN(4);
        _sdata = .;        
        *(.data)           
        *(.data*)         
        . = ALIGN(4);
        _edata = .;       
    } > SRAM AT > FLASH

    /* The references to symbols in .data section will be resolved according to the SRAM addresses;
    since it's where the .data section is loaded for execution */

    .bss :
    {
        . = ALIGN(4);
        __bss_start__ = .;    
        *(.bss)
        *(.bss*)           
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;      
    } > SRAM

    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        _heap_start = .;
        . = . + __max_heap_size;
        . = ALIGN(8);
        _heap_end = .;
        _end = .;           
    } > SRAM

    /* Stack is required by the AAPCS standard to be aligned at 8 bytes */
    /* Here, we have placed the stack as the last section, which can lead to the corruption
    of other sections in the SRAM if the stack overflows; another approach is to make stack the
    first section in the SRAM so that if it overflows, it goes into the unmapped region (below
    0x20000000) preventing corruption of any other section on SRAM and also causing the 
    Hard Fault exception */
    
    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        _stack_start = .;
        . = . + __max_stack_size;
        . = ALIGN(8);
        _stack_end = .;
    } > SRAM

    

    /DISCARD/ :
    {
        *(.comment)
        *(.ARM.attributes)
        *(.note.GNU-stack)
    }
}

_estack = ORIGIN(SRAM) + LENGTH(SRAM);