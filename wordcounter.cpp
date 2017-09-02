#include <iostream>
#include <cstddef>
#include <cstdint>
#include <xmmintrin.h>
#include <tmmintrin.h>

using namespace std;

int word_count_simple(const char* str, size_t size) {
	int ans = 0;
	bool got_space = true;

	for (size_t i = 0; i < size; i++) {
		if (got_space && str[i] != ' ')
			ans++;
		got_space = (str[i] == ' ');
	}
	
	return ans;
}

int word_count_asm(const char* str, size_t size) {

	if (size <= sizeof(__m128i) * 2)
		return word_count_simple(str, size);

	size_t curr_pos = 0;
	size_t ans = 0;
	bool is_space = false;

	while ((size_t)(str + curr_pos) % 16 != 0) {

		char c = *(str + curr_pos);
		if (is_space && c != ' ')
			ans++;

		is_space = (c == ' ');
		curr_pos++;
	}

	if (*str != ' ')
		ans++;
	if (is_space && *(str + curr_pos) != ' ' && curr_pos != 0)
		ans++;

	size_t aligned_end = size - (size - curr_pos) % 16 - 16;
	const __m128i SPACE_BYTES_16 = _mm_set1_epi8(' '); // asm DUP (?)
	__m128i byte_storage = _mm_set1_epi8(0);
	__m128i next_spaces_mask = _mm_cmpeq_epi8(_mm_load_si128((__m128i *) (str + curr_pos)), SPACE_BYTES_16); // asm PCMPEQB: ri := (ai == bi) ? 0xff : 0x0

	for (size_t i = curr_pos; i < aligned_end; i += sizeof(__m128i)) {
		__m128i curr_spaces_mask = next_spaces_mask;
		next_spaces_mask = _mm_cmpeq_epi8(_mm_load_si128((__m128i *) (str + i + 16)), SPACE_BYTES_16);
		__m128i shifted_spaces_mask = _mm_alignr_epi8(next_spaces_mask, curr_spaces_mask, 1); /* asm PALIGNR: least significant 16 bytes of (concat(param1, param2) 
																							shifted right (like >>) by param3 bytes); 
																							so 1 byte of next and 15 bytes of current */
		__m128i word_beginnings_mask = _mm_andnot_si128(shifted_spaces_mask, curr_spaces_mask); /* asm PANDN: r := (~a) & b; 
																								so if str[i] == ' ' and str[i] != ' ', the corresponding byte is 0xff */
		byte_storage = _mm_adds_epu8(_mm_and_si128(_mm_set1_epi8(1), word_beginnings_mask), byte_storage); // asm PADDUSB: ri = unsignedSaturate(ai + bi)

		if (_mm_movemask_epi8(byte_storage) != 0 || i + 16 >= aligned_end) { // asm PMOVMSKB: 16-bit mask from the most significant bits of the 16 8-bit integers, zero extends the upper bits
			byte_storage = _mm_sad_epu8(_mm_set1_epi8(0), byte_storage); // asm PSADBW: r0 := sum[0..7]((abs(ai - bi)), r4 := sum[8..15]((abs(ai - bi)), rest rj := 0x0
			ans += _mm_cvtsi128_si32(byte_storage); // asm MOVD: r := a0 (least significant 32 bits)
			byte_storage = _mm_srli_si128(byte_storage, 8); // asm PSRLDQ: shift param1 by param2 bytes, "param2 must be an immediate"
			ans += _mm_cvtsi128_si32(byte_storage);
			byte_storage = _mm_set1_epi8(0);
		}
	}

	curr_pos = aligned_end;

	if (*(str + curr_pos - 1) == ' ' && *(str + curr_pos) != ' ') 
		ans--;

	is_space = *(str + curr_pos - 1) == ' ';
	for (size_t i = curr_pos; i < size; i++){
		char cur = *(str + i);
		if (is_space && cur != ' '){
			ans++;
		}
		is_space = (cur == ' ');
	}

	return ans;

}

int main(void) {

	const char* test_string = "  abc abcdef    abcd  abcdefghij       abcdefgh  abcdefghijklmnopqrstuvwxyz  a ";
	int length = 79;
	int words = 7;
	cout << word_count_asm(test_string, length);
	return 0;
}

