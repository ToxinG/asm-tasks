#include <stdio.h>
#include <string.h>
#include <ctime>
#include <iostream>
#include <cstdint>
#include <emmintrin.h>

using namespace std;

#define N 2 << 28

void* memcpy_simple(void* dst, const void* src, size_t sz) {
	for (size_t i = 0; i < sz; i++) {
		*((char*)(dst) + i) = *((const char*)(src) + i);
	}
	return dst;
}

void* memcpy_asm(void* dst, const void* src, const size_t sz) {

/*	if (sz < 2 << 12)
		return memcpy_simple(dst, src, sz);
		*/
	
	int start_shift = 0;
	char* dst_char = (char*)dst;
	char* src_char = (char*)src;

	while ((size_t)(dst_char + start_shift) % 128 != 0 && start_shift < sz) {
		char cur_sym = *(src_char + start_shift);
		*(dst_char + start_shift) = cur_sym;
		start_shift++;
	}

	size_t size = sz - start_shift;

	size_t sz_rem = size % 128;
	size_t stages = size >> 7;

	__asm {

		mov esi, src;    //src pointer
		mov edi, dst;   //dst pointer

		mov ebx, stages;   //counter 
		jz loop_copy_end;

	loop_copy:
		prefetchnta 128[ESI]; //SSE2 prefetch
		prefetchnta 160[ESI];
		prefetchnta 192[ESI];
		prefetchnta 224[ESI]; 

		movdqu xmm0, 0[ESI]; //move data from src to registers
		movdqu xmm1, 16[ESI];
		movdqu xmm2, 32[ESI];
		movdqu xmm3, 48[ESI];
		movdqu xmm4, 64[ESI];
		movdqu xmm5, 80[ESI];
		movdqu xmm6, 96[ESI];
		movdqu xmm7, 112[ESI];

		movntdq 0[EDI], xmm0; //move data from registers to dst
		movntdq 16[EDI], xmm1;
		movntdq 32[EDI], xmm2;
		movntdq 48[EDI], xmm3;
		movntdq 64[EDI], xmm4;
		movntdq 80[EDI], xmm5;
		movntdq 96[EDI], xmm6;
		movntdq 112[EDI], xmm7;

		add esi, 128;
		add edi, 128;
		dec ebx;

		jnz loop_copy;
	loop_copy_end:
	}

	return memcpy_simple((char*)dst + sz - sz_rem, (char*)src + sz - sz_rem, sz_rem);
}

int main(void) {
	/*
	int *a = new int[300];
	int *b = new int[300];
	for (int i = 0; i < 300; i++) {
		a[i] = i;
		b[i] = 7000 + i;
	}
	memcpy_asm(a + 1, b + 7, sizeof(int) * 200);
	for (int i = 0; i < 300; i++)
		printf("%d ", a[i]);

	return 0;
	*/
	
	char *src = new char[N];
	char *dst = new char[N];

	std::clock_t simple_time = std::clock();
	memcpy_simple(dst, src, N);
	float simple_time_value = std::clock() - simple_time;
	std::cout << simple_time_value << std::endl;

	std::clock_t asm_time = std::clock();
	memcpy_asm(dst, src, N);
	float asm_time_value = std::clock() - asm_time;
	std::cout << asm_time_value << std::endl;

	std::clock_t std_time = std::clock();
	memcpy(dst, src, N);
	float std_time_value = std::clock() - std_time;
	std::cout << std_time_value << std::endl;

	std::cout << simple_time_value / asm_time_value << std::endl;

	return 0;
	
}

