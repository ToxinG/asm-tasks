#ifndef TRAMPOLINE_TRAMPOLINE_H
#define TRAMPOLINE_TRAMPOLINE_H

#include <cstdlib>
#include <sys/mman.h>
#include <xmmintrin.h>
#include <iostream>

template<typename... Args>
struct tr_args;

template<>
struct tr_args<> {
    static const int INT_PTR = 0;
    static const int SSE = 0;
};

template<typename... Args>
struct tr_args<float, Args...> {
    static const int INT_PTR = tr_args<Args ...>::INT_PTR;
    static const int SSE = tr_args<Args ...>::SSE + 1;
};

template<typename... Args>
struct tr_args<double, Args...> {
    static const int INT_PTR = tr_args<Args ...>::INT_PTR;
    static const int SSE = tr_args<Args ...>::SSE + 1;
};

template<typename... Args>
struct tr_args<__m64, Args...> {
    static const int INT_PTR = tr_args<Args ...>::INT_PTR;
    static const int SSE = tr_args<Args ...>::SSE + 1;
};

template<typename First, typename... Args>
struct tr_args<First, Args...> {
    static const int INT_PTR = tr_args<Args ...>::INT_PTR + 1;
    static const int SSE = tr_args<Args ...>::SSE;
};

namespace mem {
    void **ptr = nullptr;
    const int TRAMPOLINE_SIZE = 123;
    const int PAGES_AMOUNT = 1;
    const int PAGE_SIZE = 4096;

    void alloc() {
        void *mem = mmap(nullptr, PAGE_SIZE * PAGES_AMOUNT,
                         PROT_EXEC | PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS,
                         -1, 0);
        ptr = (void **) mem;

        for (auto i = 0; i < PAGE_SIZE * PAGES_AMOUNT; i += TRAMPOLINE_SIZE) {

            auto c = (char *)mem + i;

            *(void **)c = 0;
            if (i != 0) *(void **)(c - TRAMPOLINE_SIZE) = c;
        }

    }

    void* get_next() {
        if (ptr == nullptr) {
            alloc();
            if (ptr == nullptr) return nullptr;
        }
        void *ans = ptr;
        ptr = (void**)*ptr;
        return ans;
    }

    void free_ptr(void *p) {
        *(void **) p = ptr;
        ptr = (void **) p;
    }
}

template <typename T>
struct trampoline;

template<typename R, typename... Args>
void swap(trampoline<R(Args...)>& lhs, trampoline<R(Args...)>& rhs);

template <typename T, typename ... Args>
struct trampoline<T (Args ...)> {

private:

    const char* shifts[6] = {
            "\x48\x89\xfe" /*mov rsi rdi*/,
            "\x48\x89\xf2" /*mov rdx rsi*/,
            "\x48\x89\xd1" /*mov rcx rdx*/,
            "\x49\x89\xc8" /*mov r8 rcx;*/,
            "\x4d\x89\xc1" /*mov r9 r8;*/,
            "\x41\x51" /*push %%r9;*/
    };

    void add(char* &p, const char* command) {
        for (const char *i = command; *i; i++) *(p++) = *i;
    }

public:

    template <typename F>
    trampoline(F func) {
        func_obj = new F(std::move(func));
        deleter = my_deleter<F>;
        code = mem::get_next();
        char *pcode = (char *)code;

        if (tr_args<Args ...>::INT_PTR < 6) {
            for (int i = tr_args<Args ...>::INT_PTR - 1; i >= 0; i--) add(pcode, shifts[i]);
            add(pcode,"\x48\xbf");                                       //mov  rdi, ptr_to_func_obj
            *(void **)pcode = func_obj;
            pcode += 8;
            add(pcode, "\x48\xb8");                                      //mov  rax, address_of_do_call
            *(void **)pcode = (void *)&do_call<F>;
            pcode += 8;
            add(pcode, "\xff\xe0");                                      //jmp  rax
        } else {
            int stack_size = 8 * (tr_args<Args ...>::INT_PTR - 5 + std::max(tr_args<Args ...>::SSE - 8, 0));
            add(pcode, "\x4c\x8b\x1c\x24");                              //mov  r11 [rsp]
            for (int i = 5 ; i >= 0; i--) add(pcode, shifts[i]);
            add(pcode, "\x48\x89\xe0\x48\x05");                          //mov  rax, rsp //add  rax, stack_size
            *(int32_t *)pcode = stack_size;
            pcode += 4;
            add(pcode,"\x48\x81\xc4");                                   //add  rsp, 8
            *(int32_t *)pcode = 8;
            pcode += 4;
            char *label_1 = pcode;
            add(pcode,"\x48\x39\xe0\x74");                               //cmp rax, rsp //je
            char *label_2 = pcode;
            pcode++;
            {
                add(pcode,"\x48\x81\xc4\x08");                           //add rsp, 8
                pcode += 3;
                add(pcode, "\x48\x8b\x3c\x24\x48\x89\x7c\x24\xf8\xeb");  //mov rdi, [rsp] //mov [rsp-0x8],rdi //jmp
                *pcode = label_1 - pcode - 1;
                pcode++;
            }
            *label_2 = pcode - label_2 - 1;
            add(pcode, "\x4c\x89\x1c\x24\x48\x81\xec");                  //mov [rsp], r11 //sub rsp, stack_size
            *(int32_t *)pcode = stack_size;
            pcode += 4;
            add(pcode,"\x48\xbf");                                       //mov rdi, imm
            *(void **)pcode = func_obj;
            pcode += 8;
            add(pcode, "\x48\xb8");                                      //mov rax, imm
            *(void **)pcode = (void *)&do_call<F>;
            pcode += 8;
            add(pcode,"\xff\xd0\x41\x59\x4c\x8b\x9c\x24");               //call rax //pop r9 //mov r11,[rsp + stack_size]
            *(int32_t *)pcode = stack_size - 8;
            pcode += 4;
            add(pcode, "\x4c\x89\x1c\x24\xc3");                          //mov [rsp],r11 //return
        }
    }

    trampoline(trampoline&& other) {
        func_obj = other.func_obj;
        code = other.code;
        deleter = other.deleter;
        other.func_obj = nullptr;
    }

    trampoline(const trampoline&) = delete;

    template <class TR>
    trampoline& operator=(TR&& func) {
        trampoline tmp(std::move(func));
        ::swap(*this, tmp);
        return *this;
    }

    T (*get() const)(Args ... args) {
        return (T(*)(Args ... args))code;
    }

    void swap(trampoline &other) {
        ::swap(*this, other);
    }

    friend void ::swap<>(trampoline& a, trampoline& b);

    ~trampoline() {
        if (func_obj) deleter(func_obj);
        mem::free_ptr(code);
    }

private:

    template <typename F>
    static T do_call(void* obj, Args ...args) {
        return  (*static_cast<F*>(obj))(std::forward<Args>(args)...);
    }

    template <typename F>
    static void my_deleter(void* func_obj) {
        delete static_cast<F*>(func_obj);
    }


    void* func_obj;
    void* code;
    void (*deleter)(void*);
};

template<typename R, typename... Args>
void swap(trampoline<R(Args...)>& lhs, trampoline<R(Args...)>& rhs) {
    std::swap(lhs.func_obj, rhs.func_obj);
    std::swap(lhs.code, rhs.code);
    std::swap(lhs.deleter, rhs.deleter);
}

#endif //TRAMPOLINE_TRAMPOLINE_H
