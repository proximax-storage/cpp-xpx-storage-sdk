
/* compile with:
 on linux:   gcc -g stack_traces.c
 on OS X:    gcc -g -fno-pie stack_traces.c
 on windows: gcc -g stack_traces.c -limagehlp
 */

#include <signal.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <imagehlp.h>
#else
#include <err.h>
#include <execinfo.h>
#endif

#include <execinfo.h>
#include <cstdio>
#include <string>
#include <cstring>
#include <boost/algorithm/string.hpp>

#include "drive/log.h"

extern char gExecutablePath[PATH_MAX+1];

void PrintBacktrace(void);

/* Resolve symbol name and source location given the path to the executable
   and an address */
void addr2line( char const * const program_name, void const * const addr )
{
    char addr2line_cmd[512] = {0};
    
    /* have addr2line map the address to the relent line in the code */
#ifdef __APPLE__
    /* apple does things differently... */
    std::snprintf(addr2line_cmd, sizeof(addr2line_cmd), "atos -o %.256s %p", program_name, addr);
#else
    sprintf(addr2line_cmd,"addr2line -f -p -e %.256s %p", program_name, addr);
#endif

    auto fp = popen(addr2line_cmd, "r");
    if ( fp != nullptr )
    {
        char lineInfo[1024] = {0};
        fgets( lineInfo, 1024, fp );
        pclose(fp);

        puts( lineInfo );
    }
}


#if defined __GNUC__ && __GNUC__ >= 3 && !defined __UCLIBCXX_MAJOR__

#include <cxxabi.h>

#ifndef __linux__
static std::string demangle(char const* name)
{
    // in case this string comes
    // this is needed on linux
    char const* start = std::strchr(name, '(');
    if (start != nullptr)
    {
        ++start;
    }
    else
    {
        // this is needed on macos x
        start = strstr(name, "0x");
        if (start != nullptr)
        {
            start = std::strchr(start, ' ');
            if (start != nullptr) ++start;
            else start = name;
        }
        else start = name;
    }

    char const* end = std::strchr(start, '+');
    if (end) while (*(end-1) == ' ') --end;

    std::string in;
    if (end == nullptr) in.assign(start);
    else in.assign(start, end);

    size_t len;
    int status;
    char* unmangled = ::abi::__cxa_demangle(in.c_str(), nullptr, &len, &status);
    if (unmangled == nullptr) return in;
    std::string ret(unmangled);
    ::free(unmangled);
    return ret;
}
#endif

#elif defined _WIN32

#include "libtorrent/aux_/windows.hpp"
#include <DbgHelp.h>

static std::string demangle(char const* name)
{
    char demangled_name[256];
    if (UnDecorateSymbolName(name, demangled_name, sizeof(demangled_name), UNDNAME_NO_THROW_SIGNATURES) == 0)
        demangled_name[0] = 0;
    return demangled_name;
}

#else
static std::string demangle(char const* name) { return name; }
#endif

#ifndef __linux__
static void print_backtrace(char* out, int len, int max_depth )
{
    void* stack[50];
    int size = ::backtrace(stack, 50);
    char** symbols = ::backtrace_symbols(stack, size);

    for (int i = 1; i < size && len > 0; ++i)
    {
        int ret = std::snprintf(out, std::size_t(len), "%d: %s\n", i, demangle(symbols[i]).c_str());
        out += ret;
        len -= ret;
        if (i - 1 == max_depth && max_depth > 0) break;
    }

    ::free(symbols);
}
#endif

#ifdef _WIN32
void windows_print_stacktrace(CONTEXT* context)
{
    SymInitialize(GetCurrentProcess(), 0, true);
    
    STACKFRAME frame = { 0 };
    
    /* setup initial stack frame */
    frame.AddrPC.Offset         = context->Eip;
    frame.AddrPC.Mode           = AddrModeFlat;
    frame.AddrStack.Offset      = context->Esp;
    frame.AddrStack.Mode        = AddrModeFlat;
    frame.AddrFrame.Offset      = context->Ebp;
    frame.AddrFrame.Mode        = AddrModeFlat;
    
    while (StackWalk(IMAGE_FILE_MACHINE_I386 ,
                     GetCurrentProcess(),
                     GetCurrentThread(),
                     &frame,
                     context,
                     0,
                     SymFunctionTableAccess,
                     SymGetModuleBase,
                     0 ) )
    {
        addr2line(gExecutableName, (void*)frame.AddrPC.Offset);
    }
    
    SymCleanup( GetCurrentProcess() );
}

LONG WINAPI windows_exception_handler(EXCEPTION_POINTERS * ExceptionInfo)
{
    switch(ExceptionInfo->ExceptionRecord->ExceptionCode)
    {
        case EXCEPTION_ACCESS_VIOLATION:
            puts("Error: EXCEPTION_ACCESS_VIOLATION\n" );
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            puts("Error: EXCEPTION_ARRAY_BOUNDS_EXCEEDED\n" );
            break;
        case EXCEPTION_BREAKPOINT:
            puts("Error: EXCEPTION_BREAKPOINT\n" );
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            puts("Error: EXCEPTION_DATATYPE_MISALIGNMENT\n" );
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            puts("Error: EXCEPTION_FLT_DENORMAL_OPERAND\n" );
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            puts("Error: EXCEPTION_FLT_DIVIDE_BY_ZERO\n" );
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            puts("Error: EXCEPTION_FLT_INEXACT_RESULT\n" );
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            puts("Error: EXCEPTION_FLT_INVALID_OPERATION\n" );
            break;
        case EXCEPTION_FLT_OVERFLOW:
            puts("Error: EXCEPTION_FLT_OVERFLOW\n" );
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            puts("Error: EXCEPTION_FLT_STACK_CHECK\n" );
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            puts("Error: EXCEPTION_FLT_UNDERFLOW\n" );
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            puts("Error: EXCEPTION_ILLEGAL_INSTRUCTION\n" );
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            puts("Error: EXCEPTION_IN_PAGE_ERROR\n" );
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            puts("Error: EXCEPTION_INT_DIVIDE_BY_ZERO\n" );
            break;
        case EXCEPTION_INT_OVERFLOW:
            puts("Error: EXCEPTION_INT_OVERFLOW\n" );
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            puts("Error: EXCEPTION_INVALID_DISPOSITION\n" );
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            puts("Error: EXCEPTION_NONCONTINUABLE_EXCEPTION\n" );
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            puts("Error: EXCEPTION_PRIV_INSTRUCTION\n" );
            break;
        case EXCEPTION_SINGLE_STEP:
            puts("Error: EXCEPTION_SINGLE_STEP\n" );
            break;
        case EXCEPTION_STACK_OVERFLOW:
            puts("Error: EXCEPTION_STACK_OVERFLOW\n" );
            break;
        default:
            puts("Error: Unrecognized Exception\n" );
            break;
    }
    fflush(stderr);
    /* If this is a stack overflow then we can't walk the stack, so just show
     where the error happened */
    if (EXCEPTION_STACK_OVERFLOW != ExceptionInfo->ExceptionRecord->ExceptionCode)
    {
        windows_print_stacktrace(ExceptionInfo->ContextRecord);
    }
    else
    {
        addr2line(gExecutableName, (void*)ExceptionInfo->ContextRecord->Eip);
    }
    
    return EXCEPTION_EXECUTE_HANDLER;
}

void set_signal_handler()
{
    SetUnhandledExceptionFilter(windows_exception_handler);
}

#else // #ifdef _WIN32

#define MAX_STACK_FRAMES 64
static void *stack_traces[MAX_STACK_FRAMES];
void posix_print_stack_trace()
{
    int i, trace_size = 0;
    char **messages = (char **)NULL;
    
    trace_size = backtrace(stack_traces, MAX_STACK_FRAMES);
    messages = backtrace_symbols(stack_traces, trace_size);
    
    /* skip the first couple stack frames (as they are this function and
     our handler) and also skip the last frame as it's (always?) junk. */
    for (i = 3; i < (trace_size - 1); ++i)
    //for (i = 0; i < trace_size; ++i) // we'll use this for now so you can see what's going on
    {
        addr2line(gExecutablePath, stack_traces[i]);
    }
    if (messages) { free(messages); }
}


void posix_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
    gLogMutex.try_lock(); // maybe signal occurred in the __LOG()
    
    puts("\n");

    switch(sig)
    {
        case SIGSEGV:
            puts("SIGSEGV: Segmentation Fault\n" );
            break;
        case SIGINT:
            puts("Caught SIGINT: Interactive attention signal, (usually ctrl+c)\n" );
            break;
        case SIGFPE:
        {
            switch(siginfo->si_code)
            {
                case FPE_INTDIV:
                    puts("Caught SIGFPE: (integer divide by zero)\n" );
                    break;
                case FPE_INTOVF:
                    puts("Caught SIGFPE: (integer overflow)\n" );
                    break;
                case FPE_FLTDIV:
                    puts("Caught SIGFPE: (floating-point divide by zero)\n" );
                    break;
                case FPE_FLTOVF:
                    puts("Caught SIGFPE: (floating-point overflow)\n" );
                    break;
                case FPE_FLTUND:
                    puts("Caught SIGFPE: (floating-point underflow)\n" );
                    break;
                case FPE_FLTRES:
                    puts("Caught SIGFPE: (floating-point inexact result)\n" );
                    break;
                case FPE_FLTINV:
                    puts("Caught SIGFPE: (floating-point invalid operation)\n" );
                    break;
                case FPE_FLTSUB:
                    puts("Caught SIGFPE: (subscript out of range)\n" );
                    break;
                default:
                    puts("Caught SIGFPE: Arithmetic Exception\n" );
                    break;
            }
            break;
        }
        case SIGILL:
            switch(siginfo->si_code)
            {
                case ILL_ILLOPC:
                    puts("Caught SIGILL: (illegal opcode)\n" );
                    break;
                case ILL_ILLOPN:
                    puts("Caught SIGILL: (illegal operand)\n" );
                    break;
                case ILL_ILLADR:
                    puts("Caught SIGILL: (illegal addressing mode)\n" );
                    break;
                case ILL_ILLTRP:
                    puts("Caught SIGILL: (illegal trap)\n" );
                    break;
                case ILL_PRVOPC:
                    puts("Caught SIGILL: (privileged opcode)\n" );
                    break;
                case ILL_PRVREG:
                    puts("Caught SIGILL: (privileged register)\n" );
                    break;
                case ILL_COPROC:
                    puts("Caught SIGILL: (coprocessor error)\n" );
                    break;
                case ILL_BADSTK:
                    puts("Caught SIGILL: (internal stack error)\n" );
                    break;
                default:
                    puts("Caught SIGILL: Illegal Instruction\n" );
                    break;
            }
            break;
        case SIGTERM:
            puts("Caught SIGTERM: a termination request was sent to the program\n" );
            break;
        case SIGABRT:
            puts("Caught SIGABRT: usually caused by an abort() or assert()\n" );
            break;
        default:
            break;
    }

#ifdef __linux__
    PrintBacktrace();
#else
    if (1)
    {
        char stack[8192];
        stack[0] = '\0';
        print_backtrace( stack, sizeof(stack), 0 );

        puts( stack );
    }
    posix_print_stack_trace();
#endif
    
    _Exit(1);
}

static uint8_t alternate_stack[131072];
void set_signal_handler()
{
    /* setup alternate stack */
    {
        stack_t ss = {};
        /* malloc is usually used here, I'm not 100% sure my static allocation
         is valid but it seems to work just fine. */
        ss.ss_sp = (void*)alternate_stack;
        ss.ss_size = 131072;
        ss.ss_flags = 0;
        
        if (sigaltstack(&ss, NULL) != 0) { err(1, "sigaltstack"); }
    }
    
    /* register our signal handlers */
    {
        struct sigaction sig_action = {};
        sig_action.sa_sigaction = posix_signal_handler;
        sigemptyset(&sig_action.sa_mask);
        
#ifdef __APPLE__
        /* for some reason we backtrace() doesn't work on osx
         when we use an alternate stack */
        sig_action.sa_flags = SA_SIGINFO;
#else
        sig_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
#endif
        
        if (sigaction(SIGSEGV, &sig_action, NULL) != 0) { err(1, "sigaction"); }
        if (sigaction(SIGFPE,  &sig_action, NULL) != 0) { err(1, "sigaction"); }
        if (sigaction(SIGINT,  &sig_action, NULL) != 0) { err(1, "sigaction"); }
        if (sigaction(SIGILL,  &sig_action, NULL) != 0) { err(1, "sigaction"); }
        if (sigaction(SIGTERM, &sig_action, NULL) != 0) { err(1, "sigaction"); }
        if (sigaction(SIGABRT, &sig_action, NULL) != 0) { err(1, "sigaction"); }
    }
}
#endif
