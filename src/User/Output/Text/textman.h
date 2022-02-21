#ifndef SALERNOS_CORE_KERNEL_USER_OUTPUT_TEXT_MANAGER
#define SALERNOS_CORE_KERNEL_USER_OUTPUT_TEXT_MANAGER

    #include <kerntypes.h>


    typedef struct Textbuffer {
        uint8_t* _Buffer;
        uint64_t _BufferSize;
        uint64_t _Index;
        uint64_t _EndIndex;
    } textbuffer_t;


    /***********************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    ***********************************************************************/
    void            kernel_ktm_tbo_bind           (textbuffer_t __textbuff);
    textbuffer_t    kernel_ktm_tbo_get            ();
    void            kernel_ktm_tbo_clear          ();

    void            kernel_ktm_text_shl           (uint64_t __positions);

#endif
