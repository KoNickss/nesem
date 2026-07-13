#ifndef COMMONH
    #include <stdio.h>
    #include <stdlib.h>
    #include <stdbool.h>

    typedef unsigned char byte; //8bit (byte) variable type
    typedef unsigned short word; //16bit (2byte) variable type
    //typedef enum {false, true} bool;

    #define KB 0x0400;

    
	#define RESET_TEXT 	"\033[30;1m" //NULL
	#define WARN_TEXT 	"\033[93;1m" //yellow
	#define INFO_TEXT 	"\033[97;1m" //WHITE
	#define ERROR_TEXT 	"\033[91;1m" //Red
	#define FATAL_TEXT 	"\033[91;7m" //Red background


    #define PRINT_WARN(subject, ...) fprintf(stderr, WARN_TEXT"WARN [%s]:\t", subject);fprintf(stderr, __VA_ARGS__); fprintf(stderr, RESET_TEXT"\n");
    #define PRINT_ERROR(subject, ...) fprintf(stderr, ERROR_TEXT"ERROR [%s]:\t", subject);fprintf(stderr, __VA_ARGS__); fprintf(stderr, RESET_TEXT"\n");
    #define PRINT_INFO(subject, ...)fprintf(stderr, INFO_TEXT"INFO [%s]:\t", subject);fprintf(stderr, __VA_ARGS__); fprintf(stderr, RESET_TEXT"\n");

    #if DEBUG
        #define debug_console_end_print() fputc('\n', stderr);
        #define DWARN(...) fprintf(stderr, 	WARN_TEXT "\r%s, %s, on line %u\nWARN: ", __FILE__, __PRETTY_FUNCTION__, __LINE__);fprintf(stderr, __VA_ARGS__); fprintf(stderr, RESET_TEXT);debug_console_end_print();
        #define DFATAL(...) fprintf(stderr, FATAL_TEXT "\r%s, %s, on line %u\nFATAL ERROR: ", __FILE__, __PRETTY_FUNCTION__, __LINE__);fprintf(stderr, __VA_ARGS__);fprintf(stderr, RESET_TEXT);abort();debug_console_end_print();
        #define DERROR(...) fprintf(stderr, ERROR_TEXT "\r%s, %s, on line %u\nERROR: ", __FILE__, __PRETTY_FUNCTION__, __LINE__);fprintf(stderr, __VA_ARGS__);fprintf(stderr, RESET_TEXT);debug_console_end_print();
        #define DPERROR(...)fprintf(stderr, ERROR_TEXT "\r%s, %s, on line %u\nERROR: ", __FILE__, __PRETTY_FUNCTION__, __LINE__);fprintf(stderr, __VA_ARGS__);perror("");fprintf(stderr, RESET_TEXT);debug_console_end_print();
        #define DINFO(...) fprintf(stdout, 	INFO_TEXT "\r%s, %s, on line %u\nINFO: ", __FILE__, __PRETTY_FUNCTION__, __LINE__);fprintf(stdout, __VA_ARGS__);fprintf(stdout, RESET_TEXT);debug_console_end_print();

        #define SMART_ASSERT(expression, ...) if(!(expression)){DFATAL(__VA_ARGS__);}
        #define SMART_WARN(expression, ...) if(!(expression)){DWARN(__VA_ARGS__);}
        #define SMART_ERROR(expression, ...) if(!(expression)){DERROR(__VA_ARGS__);}
        #define SMART_INFO(expression, ...) if(!(expression)){DINFO(__VA_ARGS__);} 


        static inline void* _xmalloc(size_t size, const char* _func, const char* _file, size_t _line){
            void* ret = malloc(size);
            if(ret == NULL){
                PRINT_ERROR("malloc", "Out of memory! Attempted to allocate %lu bytes in %s:%lu in function \"%s\"", size, _file, _line, _func);
                abort();
            }
            return ret;
        }
        #define xmalloc(size) _xmalloc(size, __PRETTY_FUNCTION__, __FILE__, __LINE__)
    #else
        #define DWARN(...) ((void)0)
        #define DFATAL(...) ((void)0)
        #define DERROR(...) ((void)0)
        #define DPERROR(...) ((void)0)
        #define DINFO(...) ((void)0)

        #define SMART_ASSERT(expression, ...) ((void)0)	
        #define SMART_WARN(expression, ...)  ((void)0)
        #define SMART_ERROR(expression, ...) ((void)0)
        #define SMART_INFO(expression, ...)  ((void)0)


        static inline void* _xmalloc(size_t size){
            void* ret = malloc(size);
            if(ret == NULL){
                PRINT_ERROR("malloc", "Out of memory! Attempted to allocate %lu bytes", size);
                abort();
            }
            return ret;
        }
        #define xmalloc(size) _xmalloc(size)
    #endif

#endif

#define COMMONH
