#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
//#include "xvpi.h"

#if defined(VPI_USER_H)
	/* struct t_vpi_vecval encodes aval/bval bitfields as PLI_INT32 */
	#define WORD PLI_INT32
	#define UWORD PLI_UINT32
	#define DWORD int64_t
	#define UDWORD uint64_t
	#define BITS_PER_WORD		32
	#define LOG2_BITS_PER_WORD	5
#else
	/* fake the required bits from xvpi.h and vpi_user.h: */
	#define msg(x...)		do { (void) fprintf(stderr, x); } while (0)
	#define warn(x...)		do { (void) fprintf(stderr, "WARNING: " x); } while (0)
	#define err(x...)		do { (void) fprintf(stderr, "ERROR: " x); } while (0)
	#define fatal(x...)		do { (void) fprintf(stderr, "FATAL: " x); abort(); } while (0)

	/* choose artificially small wordsize to trigger corner cases when testing: */
	#define WORD int8_t
	#define UWORD uint8_t
	#define DWORD int16_t
	#define UDWORD uint16_t
	#define BITS_PER_WORD		8
	#define LOG2_BITS_PER_WORD	3

	typedef int32_t PLI_INT32;

	typedef struct t_vpi_vecval {
		WORD aval, bval;   /* bit encoding: ab: 00=0, 10=1, 11=X, 01=Z */
	} s_vpi_vecval, *p_vpi_vecval;

	#define vpiUndefined            -1
	#define vpi0                    0
	#define vpi1                    1
	#define vpiZ                    2
	#define vpiX                    3

	#define vpiBinStrVal            1
	#define vpiOctStrVal            2
	#define vpiDecStrVal            3
	#define vpiHexStrVal            4
#endif

#define N_WORDS(size) ((size + BITS_PER_WORD - 1) / BITS_PER_WORD)
#define WORD_PADDING_MASK(size) ((UWORD) ~((UWORD) 0) >> ((BITS_PER_WORD - 1) - ((size - 1) % BITS_PER_WORD)))

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define abs(x) ((x) < 0 ? -(x) : (x))

struct vec4state {
	size_t def_size;
	size_t size;
	int is_signed;
	struct t_vpi_vecval *words;
};

static const struct t_vpi_vecval const_vecval_zero [1] = { { .aval = 0, .bval = 0 } };

static const struct t_vpi_vecval const_vecval_one [1] = { { .aval = 1, .bval = 0 } };

static const struct t_vpi_vecval const_vecval_minus_one [1] = { { .aval = -1, .bval = 0 } };

static const struct vec4state vec4state_const_zero = {
	.def_size = 1,
	.size = 1,
	.is_signed = 0,
	.words = (struct t_vpi_vecval *) const_vecval_zero
};

static const struct vec4state vec4state_const_one = {
	.def_size = 32,
	.size = 1,
	.is_signed = 0,
	.words = (struct t_vpi_vecval *) const_vecval_one
};

static const struct vec4state vec4state_const_minus_one = {
	.def_size = 32,
	.size = 1,
	.is_signed = 1,
	.words = (struct t_vpi_vecval *) const_vecval_minus_one
};

static void vec4state_clear_padding_bits (struct vec4state *v)
{
	v->words[N_WORDS(v->size)-1].aval &= WORD_PADDING_MASK(v->size);
	v->words[N_WORDS(v->size)-1].bval &= WORD_PADDING_MASK(v->size);
}

static inline PLI_INT32 vec4state_bit_test (const struct vec4state *v, unsigned bit)
{
	unsigned word_idx = bit / BITS_PER_WORD;
	unsigned bit_idx = bit % BITS_PER_WORD;
	PLI_INT32 bit_01 = (v->words[word_idx].aval >> bit_idx) & 1;
	PLI_INT32 bit_XZ = (v->words[word_idx].bval >> bit_idx) & 1;
	return ((bit_XZ << 1) | bit_01); /* vpi0 = 0, vpi1 = 1, vpiZ = 2, vpiX = 3 */
}

static inline void vec4state_bit_set (struct vec4state *v, unsigned bit, PLI_INT32 value)
{
	unsigned word_idx = bit / BITS_PER_WORD;
	unsigned bit_idx = bit % BITS_PER_WORD;
	/* vpi0 = 0, vpi1 = 1, vpiZ = 2, vpiX = 3 */
	WORD bit_01 = value & 1;
	WORD bit_XZ = (value >> 1) & 1;
	WORD mask_01 = bit_01 << bit_idx;
	WORD mask_XZ = bit_XZ << bit_idx;
	WORD clear_mask = ~(1 << bit_idx);
	v->words[word_idx].aval = (v->words[word_idx].aval & clear_mask) | mask_01;
	v->words[word_idx].bval = (v->words[word_idx].bval & clear_mask) | mask_XZ;
}

static inline void vec4state_bitfield_set (struct vec4state *v, unsigned start, unsigned end, PLI_INT32 value)
{
	unsigned start_word = start / BITS_PER_WORD;
	unsigned end_word = end / BITS_PER_WORD;
	UWORD a = (value == vpi0 || value == vpiZ) ? 0 : ~((UWORD) 0);
	UWORD b = (value == vpi0 || value == vpi1) ? 0 : ~((UWORD) 0);
	int i;
	for (i=start_word; i<=end_word; i++) {
		if (i != start_word && i != end_word) {
			v->words[i].aval = a;
			v->words[i].bval = b;
		} else {
			UWORD mask = ~((UWORD) 0);
			if (i == start_word)
				mask &= (~((UWORD) 0)) << (start % BITS_PER_WORD);
			if (i == end_word)
				mask &= (~((UWORD) 0)) >> ((end + 1) % BITS_PER_WORD);
			v->words[i].aval = (v->words[i].aval & ~mask) | (a & mask);
			v->words[i].bval = (v->words[i].bval & ~mask) | (b & mask);
		}
	}
}

/* valid values: -vpi1/vpi0/vpi1/vpiX/vpiZ (-vpi1 == (signed) -1, is handled implicitly): */
static inline void vec4state_set (struct vec4state *v, PLI_INT32 value)
{
	WORD a0 = (value == vpi0) ? 0 : (value == vpi1) ? 1 : -1;
	WORD a = (value == vpi0 || value == vpi1) ? 0 : -1;
	WORD b = (value == vpiX || value == vpiZ) ? -1 : 0;
	int i;
	v->words[0].aval = a0;
	v->words[0].bval = b;
	for (i=1; i<N_WORDS(v->size); i++) {
		v->words[i].aval = a;
		v->words[i].bval = b;
	}
	vec4state_clear_padding_bits(v);
}

static inline int vec4state_any_bit_is_X_or_Z (const struct vec4state *v)
{
	int i;
	for (i=0; i<N_WORDS(v->size); i++) {
		if (v->words[i].bval != 0)
			return 1;
	}
	return 0;
}

struct vec4state * vec4state_new (size_t size, int is_signed)
{
	struct vec4state *v;
	/**  Compile-time sanity check of used types.
	 *   If everything is alright, this becomes a no-op, and the compiler will optimize it away.
	 */
	if (BITS_PER_WORD != CHAR_BIT * sizeof(WORD) || BITS_PER_WORD != (1 << LOG2_BITS_PER_WORD)) {
		fatal("Base type definition mismatch!\n");
		return NULL;
	}
	if (sizeof(WORD) != sizeof(v->words[0].aval) || sizeof(WORD) != sizeof(v->words[0].bval)) {
		fatal("Base type definition mismatch!\n");
		return NULL;
	}
	if (sizeof(DWORD) != 2 * sizeof(WORD) || sizeof(UDWORD) != 2 * sizeof(UWORD)) {
		fatal("Base type size mismatch!\n");
		return NULL;
	}
	if ((v = malloc(sizeof(*v))) == NULL) {
		fatal("memory allocation for vec4state failed!");
		return NULL;
	}
	v->def_size = 0;
	v->size = size;
	v->is_signed = is_signed;
	v->words = malloc(sizeof(v->words[0]) * N_WORDS(size));
	if (v->words == NULL) {
		free(v);
		fatal("failed allocating %lu bytes for vec4state!", (unsigned long) sizeof(v->words[0]) * N_WORDS(size));
		return NULL;
	}
	vec4state_set(v, vpiX);
	return v;
}

int vec4state_resize (struct vec4state *v, size_t new_size)
{
	size_t old_size = v->size;
	if (N_WORDS(old_size) != N_WORDS(new_size)) {
		struct t_vpi_vecval *new_word = realloc(v->words, sizeof(v->words[0]) * N_WORDS(new_size));
		if (new_word == NULL) {
			fatal("failed re-allocating %lu bytes for vec4state!", (unsigned long) sizeof(v->words[0]) * N_WORDS(new_size));
			return 0;
		}
		v->words = new_word;
	}
	v->size = new_size;
	if (old_size < new_size) {
		/* Perform sign extension/padding up to new_size. Unsigned vectors extend 0/X/Z, signed ones 0/1/X/Z: */
		PLI_INT32 sign = vec4state_bit_test(v, old_size - 1);
		if (!v->is_signed && sign == vpi1)
			sign = vpi0;
		vec4state_bitfield_set(v, old_size, new_size - 1, sign);
	}
	vec4state_clear_padding_bits(v);
	return 1;
}

void vec4state_free (struct vec4state *v)
{
	if (v) {
		free(v->words);
		free(v);
	}
}

static inline void vec4state_copy (const struct vec4state *v, struct vec4state *result)
{
	int i;
	for (i=0; i<N_WORDS(result->size); i++) {
		result->words[i].aval = (i < N_WORDS(v->size)) ? v->words[i].aval : 0;
		result->words[i].bval = (i < N_WORDS(v->size)) ? v->words[i].bval : 0;
	}
	vec4state_clear_padding_bits(result);
}

static inline WORD vec4state_padded_signed_word_a (const struct vec4state *v, int word_idx)
{
	WORD sign_word;
	if (word_idx < N_WORDS(v->size) - 1)
		return v->words[word_idx].aval;
	sign_word = (vec4state_bit_test(v, v->size - 1) == vpi0) ? 0 : -1;
	if (word_idx == N_WORDS(v->size) - 1)
		sign_word = (sign_word & ~WORD_PADDING_MASK(v->size)) | v->words[word_idx].aval;
	return sign_word;
}

PLI_INT32 vec4state_cmp (const struct vec4state *v1, const struct vec4state *v2)
{
	int i;
	/* Return vpiX if any input vector contains a vpiX or vpiZ bit: */
	if (vec4state_any_bit_is_X_or_Z(v1) || vec4state_any_bit_is_X_or_Z(v2))
		return vpiX;
	/** This condition seems counter-intuitive. But: cf. 1364-2005, sec. 4.1.7:
	 *   "When two operands of unequal bit lengths are used and one or both of the operands is unsigned,
	 *    the smaller operand shall be zero filled on the most significant bit side to extend to the size
	 *    of the larger operand."
	 */
	if (v1->size != v2->size && (!v1->is_signed || !v2->is_signed)) {
		for (i=N_WORDS(max(v1->size, v2->size))-1; i>=0; i--) {
			UWORD a1 = (i < N_WORDS(v1->size)) ? v1->words[i].aval : 0;
			UWORD a2 = (i < N_WORDS(v2->size)) ? v2->words[i].aval : 0;
			if (a1 < a2)
				return -vpi1;
			else if (a1 > a2)
				return vpi1;
		}
	} else {
		for (i=N_WORDS(max(v1->size, v2->size))-1; i>=0; i--) {
			WORD a1 = vec4state_padded_signed_word_a(v1, i);
			WORD a2 = vec4state_padded_signed_word_a(v2, i);
			if (a1 < a2)
				return -vpi1;
			else if (a1 > a2)
				return vpi1;
		}
	}
	return vpi0;
}

PLI_INT32 vec4state_case_equality (const struct vec4state *v1, const struct vec4state *v2)
{
	int i;
	if (v1->size != v2->size)
		return 0;
	for (i=0; i<N_WORDS(v1->size); i++) {
		if (v1->words[i].aval != v2->words[i].aval || v1->words[i].bval != v2->words[i].bval)
			return 0;
	}
	return 1;
}

/**
 * Truth table used for Karnaugh map and boolean minimization:
 * cf. 1364-2005, section 4.1.10, table 19:
 *   0 (00) & 0 (00) -> 0 (00)
 *   0 (00) & Z (01) -> 0 (00)
 *   0 (00) & 1 (10) -> 0 (00)
 *   0 (00) & X (11) -> 0 (00)
 *   Z (01) & 0 (00) -> 0 (00)
 *   Z (01) & Z (01) -> X (11)
 *   Z (01) & 1 (10) -> X (11)
 *   Z (01) & X (11) -> X (11)
 *   1 (10) & 0 (00) -> 0 (00)
 *   1 (10) & Z (01) -> X (11)
 *   1 (10) & 1 (10) -> 1 (10)
 *   1 (10) & X (11) -> X (11)
 *   X (11) & 0 (00) -> 0 (00)
 *   X (11) & Z (01) -> X (11)
 *   X (11) & 1 (10) -> X (11)
 *   X (11) & X (11) -> X (11)
 */
void vec4state_bitwise_binary_and (const struct vec4state *v1, const struct vec4state *v2, struct vec4state *result)
{
	int i;
	for (i=0; i<N_WORDS(result->size); i++) {
		UWORD a = (i < N_WORDS(v1->size)) ? v1->words[i].aval : 0;
		UWORD b = (i < N_WORDS(v1->size)) ? v1->words[i].bval : 0;
		UWORD c = (i < N_WORDS(v2->size)) ? v2->words[i].aval : 0;
		UWORD d = (i < N_WORDS(v2->size)) ? v2->words[i].bval : 0;
		result->words[i].aval = (b & d) | (b & c) | (a & d) | (a & c);
		result->words[i].bval = (b & d) | (b & c) | (a & d);
	}
}

/**
 * Truth table used for Karnaugh map and boolean minimization:
 * cf. 1364-2005, section 4.1.10, table 20:
 *   0 (00) | 0 (00) -> 0 (00)
 *   0 (00) | Z (01) -> X (11)
 *   0 (00) | 1 (10) -> 1 (10)
 *   0 (00) | X (11) -> X (11)
 *   Z (01) | 0 (00) -> X (11)
 *   Z (01) | Z (01) -> X (11)
 *   Z (01) | 1 (10) -> 1 (10)
 *   Z (01) | X (11) -> X (11)
 *   1 (10) | 0 (00) -> 1 (10)
 *   1 (10) | Z (01) -> 1 (10)
 *   1 (10) | 1 (10) -> 1 (10)
 *   1 (10) | X (11) -> 1 (10)
 *   X (11) | 0 (00) -> X (11)
 *   X (11) | Z (01) -> X (11)
 *   X (11) | 1 (10) -> 1 (10)
 *   X (11) | X (11) -> X (11)
 */
void vec4state_bitwise_binary_or (const struct vec4state *v1, const struct vec4state *v2, struct vec4state *result)
{
	int i;
	for (i=0; i<N_WORDS(result->size); i++) {
		UWORD a = (i < N_WORDS(v1->size)) ? v1->words[i].aval : 0;
		UWORD b = (i < N_WORDS(v1->size)) ? v1->words[i].bval : 0;
		UWORD c = (i < N_WORDS(v2->size)) ? v2->words[i].aval : 0;
		UWORD d = (i < N_WORDS(v2->size)) ? v2->words[i].bval : 0;
		result->words[i].aval = a | b | c | d;
		result->words[i].bval = (~a & d) | (b & ~c) | (b & d);
	}
}

/**
 * Truth table used for Karnaugh map and boolean minimization:
 * cf. 1364-2005, section 4.1.10, table 21:
 *   0 (00) ^ 0 (00) -> 0 (00)
 *   0 (00) ^ Z (01) -> X (11)
 *   0 (00) ^ 1 (10) -> 1 (10)
 *   0 (00) ^ X (11) -> X (11)
 *   Z (01) ^ 0 (00) -> X (11)
 *   Z (01) ^ Z (01) -> X (11)
 *   Z (01) ^ 1 (10) -> X (11)
 *   Z (01) ^ X (11) -> X (11)
 *   1 (10) ^ 0 (00) -> 1 (10)
 *   1 (10) ^ Z (01) -> X (11)
 *   1 (10) ^ 1 (10) -> 0 (00)
 *   1 (10) ^ X (11) -> X (11)
 *   X (11) ^ 0 (00) -> X (11)
 *   X (11) ^ Z (01) -> X (11)
 *   X (11) ^ 1 (10) -> X (11)
 *   X (11) ^ X (11) -> X (11)
 */
void vec4state_bitwise_binary_xor (const struct vec4state *v1, const struct vec4state *v2, struct vec4state *result)
{
	int i;
	for (i=0; i<N_WORDS(result->size); i++) {
		UWORD a = (i < N_WORDS(v1->size)) ? v1->words[i].aval : 0;
		UWORD b = (i < N_WORDS(v1->size)) ? v1->words[i].bval : 0;
		UWORD c = (i < N_WORDS(v2->size)) ? v2->words[i].aval : 0;
		UWORD d = (i < N_WORDS(v2->size)) ? v2->words[i].bval : 0;
		result->words[i].aval = d | b | (~a & c) | (a & ~c);
		result->words[i].bval = d | b;
	}
}

/**
 * cf. 1364-2005, section 4.1.10, table 23:
 *   0 (00) -> 1 (10)
 *   1 (10) -> 0 (00)
 *   X (11) -> X (11)
 *   Z (01) -> X (11)
 */
void vec4state_bitwise_unary_negation (const struct vec4state *v, struct vec4state *result)
{
	int i;
	for (i=0; i<N_WORDS(result->size); i++) {
		UWORD a = (i < N_WORDS(v->size)) ? v->words[i].aval : 0;
		UWORD b = (i < N_WORDS(v->size)) ? v->words[i].bval : 0;
		result->words[i].aval = ~a | b;
		result->words[i].bval = b;
	}
	vec4state_clear_padding_bits(result);
}

/* result->size == 1 */
void vec4state_reduction_unary_and (const struct vec4state *v, struct vec4state *result)
{
	int i;
	/* If any bit is vpi0, return vpi0: */
	for (i=0; i<N_WORDS(v->size)-1; i++) {
		if ((v->words[i].aval | v->words[i].bval) != ~((WORD) 0)) {
			vec4state_set(result, vpi0);
			return;
		}
	}
	/* check only significant bits in last words, If any bit is vpi0, return vpi0: */
	if ((v->words[N_WORDS(v->size)-1].aval | v->words[N_WORDS(v->size)-1].bval) != WORD_PADDING_MASK(v->size)) {
		vec4state_set(result, vpi0);
		return;
	}
	/* if any bit is vpiX or vpiZ, return vpiX */
	for (i=0; i<N_WORDS(v->size); i++) {
		if (v->words[i].bval != 0) {
			vec4state_set(result, vpiX);
			return;
		}
	}
	vec4state_set(result, vpi1);
}

/* result->size == 1 */
void vec4state_reduction_unary_or (const struct vec4state *v, struct vec4state *result)
{
	int i;
	/* If any bit is vpi1, return vpi1: */
	for (i=0; i<N_WORDS(v->size); i++) {
		if ((v->words[i].aval & ~v->words[i].bval) != 0) {
			vec4state_set(result, vpi1);
			return;
		}
	}
	/* if any bit is vpiX or vpiZ, return vpiX */
	for (i=0; i<N_WORDS(v->size); i++) {
		if (v->words[i].bval != 0) {
			vec4state_set(result, vpiX);
			return;
		}
	}
	vec4state_set(result, vpi0);
}

static inline int count_bits (UWORD w)
{
#if defined(__GNUC__)
	/* Try to use GCC builtins, the compiler should optimize away unused code paths: */
	if (sizeof(w) == sizeof(unsigned int))
		return __builtin_popcount(w);
	else if (sizeof(w) == sizeof(unsigned long))
		return __builtin_popcountl(w);
	else if (sizeof(w) == sizeof(unsigned long long))
		return __builtin_popcountll(w);
	else 
#endif
	/* cf. http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetKernighan: */
	{
		unsigned count;
		for (count=0; w; count++)
			w &= w - 1;	/* clear the least significant bit set */
		return count;
	}
}

/* result->size == 1 */
void vec4state_reduction_unary_xor (const struct vec4state *v, struct vec4state *result)
{
	int num_vpi1 = 0;
	int i;
	for (i=0; i<N_WORDS(v->size); i++) {
		/* if any bit is vpiX or vpiZ, return vpiX */
		if (v->words[i].bval != 0) {
			vec4state_set(result, vpiX);
			return;
		}
		num_vpi1 += count_bits(v->words[i].aval);
	}
	vec4state_set(result, (num_vpi1 & 1) ? vpi1 : vpi0);
}

void vec4state_shift_right (const struct vec4state *v, int n, struct vec4state *result, int arith)
{
	int word_shift;
	unsigned bit_shift;
	UDWORD a = 0;
	UDWORD b = 0;
	int i, j;
	if (n > 0) {
 		word_shift = n / BITS_PER_WORD;
 		bit_shift = n % BITS_PER_WORD;
		j = -word_shift;
	} else {
 		word_shift = -n / BITS_PER_WORD;
		bit_shift = BITS_PER_WORD - (-n % BITS_PER_WORD);
		j = word_shift - 1;
	}
	if (j >= 0 && j < N_WORDS(v->size)) {
		a = (UDWORD) (UWORD) v->words[j].aval;
		b = (UDWORD) (UWORD) v->words[j].bval;
	}
	for (i=0; i<N_WORDS(result->size); i++) {
		j++;
		if (j >= 0 && j < N_WORDS(v->size)) {
			a |= ((UDWORD) (UWORD) v->words[j].aval) << BITS_PER_WORD;
			b |= ((UDWORD) (UWORD) v->words[j].bval) << BITS_PER_WORD;
		}
		result->words[i].aval = a >> bit_shift;
		result->words[i].bval = b >> bit_shift;
		a >>= BITS_PER_WORD;
		b >>= BITS_PER_WORD;
	}
	if (arith) {
		PLI_INT32 sign = vec4state_bit_test(v, v->size - 1);
		vec4state_bitfield_set(result, result->size - 1 - n, result->size - 1, sign);
	}
	vec4state_clear_padding_bits(result);
}

void vec4state_shift_left (const struct vec4state *v, int n, struct vec4state *result)
{
	vec4state_shift_right(v, -n, result, 0);
}

void vec4state_add_unsigned_word (const struct vec4state *v, UWORD word, struct vec4state *result)
{
	UDWORD r = word;
	int i;
	for (i=0; i<N_WORDS(result->size); i++) {
		/* Return vpiX if any input vector contains a vpiX or vpiZ bit: */
		if (v->words[i].bval != 0) {
			vec4state_set(result, vpiX);
			return;
		}
		r += (UWORD) v->words[i].aval;
		result->words[i].aval = r;
		result->words[i].bval = 0;
		r >>= BITS_PER_WORD;
	}
	vec4state_clear_padding_bits(result);
}

void vec4state_add (const struct vec4state *v1, const struct vec4state *v2, struct vec4state *result)
{
	UDWORD r;
	int i;
	for (r=0, i=0; i<N_WORDS(result->size); i++) {
		UWORD a1 = (i < N_WORDS(v1->size)) ? v1->words[i].aval : 0;
		UWORD b1 = (i < N_WORDS(v1->size)) ? v1->words[i].bval : 0;
		UWORD a2 = (i < N_WORDS(v2->size)) ? v2->words[i].aval : 0;
		UWORD b2 = (i < N_WORDS(v2->size)) ? v2->words[i].bval : 0;
		/* Return vpiX if any input vector contains a vpiX or vpiZ bit: */
		if (b1 != 0 || b2 != 0) {
			vec4state_set(result, vpiX);
			return;
		}
		r = (r >> BITS_PER_WORD) + a1 + a2;
		result->words[i].aval = r;
		result->words[i].bval = 0;
	}
}

void vec4state_sub (const struct vec4state *v1, const struct vec4state *v2, struct vec4state *result)
{
	UDWORD r;
	int i;
	for (r=0, i=0; i<N_WORDS(result->size); i++) {
		UWORD a1 = (i < N_WORDS(v1->size)) ? v1->words[i].aval : 0;
		UWORD b1 = (i < N_WORDS(v1->size)) ? v1->words[i].bval : 0;
		UWORD a2 = (i < N_WORDS(v2->size)) ? v2->words[i].aval : 0;
		UWORD b2 = (i < N_WORDS(v2->size)) ? v2->words[i].bval : 0;
		/* Return vpiX if any input vector contains a vpiX or vpiZ bit: */
		if (b1 != 0 || b2 != 0) {
			vec4state_set(result, vpiX);
			return;
		}
		r = (r >> BITS_PER_WORD) + a1 - a2;
		result->words[i].aval = r;
		result->words[i].bval = 0;
	}
	vec4state_clear_padding_bits(result);
}

static inline UDWORD add3 (UDWORD a, UDWORD b, UDWORD *carry)
{
	UDWORD c = *carry;
	UDWORD ab = a + b;
	UDWORD abc = ab + c;
	c = 0;
	if (ab < b)
		c++;
	if (abc < ab)
		c++;
	if (abc < a)
		c++;
	*carry = c;
	return abc;
}

/* result->size = v->size + BITS_PER_WORD */
void vec4state_mul_unsigned_word (const struct vec4state *v, UWORD word, struct vec4state *result)
{
	UDWORD r, carry;
	int i;
	if (vec4state_any_bit_is_X_or_Z(v)) {
		vec4state_set(result, vpiX);
		return;
	}
	for (r=0, carry=0, i=0; i<N_WORDS(result->size); i++) {
		UWORD a = (i < N_WORDS(v->size)) ? v->words[i].aval : 0;
		UDWORD p = (UDWORD) a * (UDWORD) word;
		r = add3(r, p, &carry);
		result->words[i].aval = r;
		result->words[i].bval = 0;
		r >>= BITS_PER_WORD;
	}
	vec4state_clear_padding_bits(result);
}

/* result->size = v1->size + v2->size */
void vec4state_mul (const struct vec4state *v1, const struct vec4state *v2, struct vec4state *result)
{
	UDWORD r, carry;
	int i, j;
	if (vec4state_any_bit_is_X_or_Z(v1) || vec4state_any_bit_is_X_or_Z(v2)) {
		vec4state_set(result, vpiX);
		return;
	}
	for (r=0, carry=0, i=0; i<N_WORDS(result->size); i++) {
		for (j=0; j<N_WORDS(v2->size); j++) {
			unsigned ii = i - j;
			unsigned jj = j;
			if (ii < N_WORDS(v1->size) && jj < N_WORDS(v2->size)) {
				UDWORD p = (UDWORD) (UWORD) v1->words[ii].aval * (UDWORD) (UWORD) v2->words[jj].aval;
				r = add3(r, p, &carry);
			}
		}
		result->words[i].aval = r;
		result->words[i].bval = 0;
		r >>= BITS_PER_WORD;
	}
}

/* Divide v by word. result->size = v->size. */
UWORD vec4state_div_unsigned_word (const struct vec4state *v, UWORD word, struct vec4state *result)
{
	UDWORD product, quotient, remainder;
	int i;
	if (vec4state_any_bit_is_X_or_Z(v)) {
		vec4state_set(result, vpiX);
		return 0;
	}
	remainder = 0;
	for (i=N_WORDS(result->size)-1; i>=0; i--) {
		product = (remainder << BITS_PER_WORD) | (UWORD) v->words[i].aval;
		remainder = product % (UDWORD) word;
		quotient = product / (UDWORD) word;
		result->words[i].aval = (UWORD) quotient;
		result->words[i].bval = 0;
	}
	vec4state_clear_padding_bits(result);
	return remainder;
}

/* cf. Knuth, Seminumerical Algorithms, section 3.1. The Classical Algorithms (4: Division) */
/* quotient = x / y, r = x - y * quotient */
/* u->size = v->size + quotient->size */
/* remainder->size = v->size */
void vec4state_div_unsigned (const struct vec4state *x, const struct vec4state *y,
			     struct vec4state *quotient, struct vec4state *remainder)
{
	int n = N_WORDS(y->size);
	/* First... normalize y: */
	while (n > 0 && y->words[n-1].aval == 0)
		n--;
	/* Division by zero or any variable undefined? then return vpiX: */
	if (n == 0 || vec4state_any_bit_is_X_or_Z(x) || vec4state_any_bit_is_X_or_Z(y)) {
		vec4state_set(quotient, vpiX);
		vec4state_set(remainder, vpiX);
	} else {
		int m = N_WORDS(quotient->size);
		/* Knuth counts from (1...m/n/m+n), not (m-1/n-1/m+n-1...0). Index 0 is unused. Right padding word required. */
		UWORD u [1 + m + n + 1];
		UWORD v [1 + n + 1];
		UWORD q [1 + m];
		UDWORD carry, r;
		UDWORD q_hat;
		int i, j;
		/* Rewrite variables to follow Knuth's notation style (msb word = index 1, lsb word = index n): */
		/* [includes Step D1: Normalize -- because n has been trimmed above] */
		for (i=1; i<=m+n; i++)
			u[i] = x->words[m+n-i].aval;
		v[m+n+1] = 0;
		for (i=1; i<=n; i++)
			v[i] = y->words[n-i].aval;
		v[n+1] = 0;
		/* Step D2: Initialize j ... D7: Loop on j: */
		for (j=1; j<=m; j++) {
			/* Step D3a: calculate q_hat: */
			if (u[j] == v[1])
				q_hat = (1 << BITS_PER_WORD) - 1;
			else
				q_hat = (((UDWORD) u[j] << BITS_PER_WORD) | u[j+1]) / v[1];
			/* Step D3b: while (v2 * q_hat > (u[j] * b + u[j+1] - q_hat * v[1]) * b + u[j+2]), decrease q_hat: */
			while (1) {
				UDWORD r_hat = (((UDWORD) u[j] << BITS_PER_WORD) | u[j+1]) - q_hat * v[1];
				UDWORD v2_qhat = v[2] * q_hat;
				if (v2_qhat < r_hat || r_hat >= (1 << BITS_PER_WORD))
					break;
				if (v2_qhat <= ((r_hat << BITS_PER_WORD) | u[j+2]))
					break;
				q_hat -= 1;
			}
			/* Step D4: Multiply and subtract: */
			for (r=0, carry=0, i=n; i>=1; i--) {
				r = add3(r + u[j+i], -q_hat * v[i], &carry);
				u[j+i] = r;
				r >>= BITS_PER_WORD;
			}
#if 0
			/* XXX CHECKME: ??D5 and D6 are implicitly performed by add3 in D4?? */
			/* Step D5: Test remainder: */
			if (carry != 0) {
				/* Step D6: Add back: */
				for (carry=0, i=n; i>=1; i--) {
					carry = (carry << BITS_PER_WORD) | v[i];
					u[j+i] += carry;
				}
				q[j] -= 1;
			}
#endif
			q[j] = q_hat;
		}
		/* Step D8: Unnormalize: */
		/* Now q contains the normalized quotient, u the normalized remainder. Store results: */
		if (quotient != NULL) {
			for (i=0; i<N_WORDS(quotient->size); i++) {
				quotient->words[i].aval = (i >= m) ? 0 : q[m-i];
				quotient->words[i].bval = 0;
			}
		}
		if (remainder != NULL) {
			for (i=0; i<N_WORDS(remainder->size); i++) {
				remainder->words[i].aval = (i >= n) ? 0 : u[m+n-i];
				remainder->words[i].bval = 0;
			}
		}
	}
}

/* result->size == [exponent b] * a->size */
void vec4state_pow (const struct vec4state *a, const struct vec4state *b, struct vec4state *result)
{
	PLI_INT32 a_cmp_0 = vec4state_cmp(a, &vec4state_const_zero);
	PLI_INT32 b_cmp_0 = vec4state_cmp(b, &vec4state_const_zero);
	PLI_INT32 a_cmp_1 = vec4state_cmp(a, &vec4state_const_one);
	PLI_INT32 a_cmp_m1 = vec4state_cmp(a, &vec4state_const_minus_one);
	if (vec4state_any_bit_is_X_or_Z(a) || vec4state_any_bit_is_X_or_Z(b)) {
		vec4state_set(result, vpiX);
	} else if (b_cmp_0 == 0 || a_cmp_1 == 0) {
		vec4state_set(result, vpi1);
	} else if (a_cmp_m1 == 0) {
		vec4state_set(result, (b->words[0].aval & 1) ? -vpi1 : vpi1);
	} else if (b_cmp_0 < 0) {
		vec4state_set(result, vpi0);
	} else if (a_cmp_0 == 0) {
		vec4state_set(result, (b_cmp_0 < 0) ? vpiX : vpi0);
	} else {
		if (b->size > 32) {
			fatal("vec4state_pow(): exponent overflow! (runtime may exceed your lifetime.)!\n");
		} else {
			struct vec4state *tmp = vec4state_new(result->size, result->is_signed);
			UWORD n = b->words[0].aval;
			int i;
			vec4state_set(result, vpi1);
			for (i=0; i<n; i++) {
				vec4state_copy(result, tmp);
				vec4state_mul(tmp, a, result);
			}
			vec4state_free(tmp);
		}
	}
}

/* cf. http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */
static inline UWORD word_reverse_bits (UWORD w)
{
	unsigned s = sizeof(w) * CHAR_BIT;
	UWORD mask = ~((UWORD) 0);
	while ((s >>= 1) > 0) {
		mask ^= (mask << s);
		w = ((w >> s) & mask) | ((w << s) & ~mask);
	}
	return w;
}

static void vec4state_get_word_at_bitpos (const struct vec4state *v, int lsb_pos, WORD *aval, WORD *bval)
{
	int word_idx = lsb_pos >> LOG2_BITS_PER_WORD;
	if (word_idx >= (int) N_WORDS(v->size)) {
		*aval = 0;
		*bval = 0;
	} else {
		unsigned bit_shift = lsb_pos & ((1 << LOG2_BITS_PER_WORD) - 1);
		int hi = word_idx + 1;
		UDWORD a;
		UDWORD b;
		if (word_idx < 0) {
			a = 0;
			b = 0;
		} else {
			a = (UDWORD) (UWORD) v->words[word_idx].aval;
			b = (UDWORD) (UWORD) v->words[word_idx].bval;
		}
		if (hi >= 0 && hi < (int) N_WORDS(v->size)) {
			a |= ((UDWORD) (UWORD) v->words[hi].aval) << BITS_PER_WORD;
			b |= ((UDWORD) (UWORD) v->words[hi].bval) << BITS_PER_WORD;
		}
		a >>= bit_shift;
		b >>= bit_shift;
		*aval = a;
		*bval = b;
	}
}

/* result->size == v->size - start */
void vec4state_bitselect_get (const struct vec4state *v, unsigned start, struct vec4state *result, int reverse)
{
	int i;
	if (!reverse) {
		vec4state_shift_right(v, start, result, 0);
	} else {
		for (i=0, start=result->size-BITS_PER_WORD+start; i<N_WORDS(result->size); i++, start-=BITS_PER_WORD) {
			WORD a, b;
			vec4state_get_word_at_bitpos(v, start, &a, &b);
			result->words[i].aval = word_reverse_bits(a);
			result->words[i].bval = word_reverse_bits(b);
		}
	}
	vec4state_clear_padding_bits(result);
}

/* result->size >= abs(end - start) + 1, v->size == abs(end - start) + 1 */
void vec4state_bitselect_set (const struct vec4state *v, unsigned start, unsigned end, struct vec4state *result)
{
	if (start < end) {
		unsigned bit_shift = BITS_PER_WORD - (start % BITS_PER_WORD);
		unsigned start_word = start / BITS_PER_WORD;
		unsigned end_word = end / BITS_PER_WORD;
		unsigned i, j;
		UDWORD a = 0;
		UDWORD b = 0;
		for (i=0, j=start_word; j<=end_word; i++, j++) {
			if (i < N_WORDS(v->size)) {
				a |= ((UDWORD) (UWORD) v->words[i].aval) << BITS_PER_WORD;
				b |= ((UDWORD) (UWORD) v->words[i].bval) << BITS_PER_WORD;
			}
			if (j != start_word && j != end_word) {
				result->words[j].aval = a >> bit_shift;
				result->words[j].bval = b >> bit_shift;
			} else {
				UWORD mask = ~((UWORD) 0);
				if (j == start_word)
					mask &= ~((1 << (start % BITS_PER_WORD)) - 1);
				if (j == end_word)
					mask &= (1 << ((end + 1) % BITS_PER_WORD)) - 1;
				result->words[j].aval = (result->words[j].aval & ~mask) | ((a >> bit_shift) & mask);
				result->words[j].bval = (result->words[j].bval & ~mask) | ((b >> bit_shift) & mask);
			}
			a >>= BITS_PER_WORD;
			b >>= BITS_PER_WORD;
		}
	} else {
		unsigned bit_shift = BITS_PER_WORD - ((start + 1) % BITS_PER_WORD);
		unsigned i;
		int start_word = start / BITS_PER_WORD;
		int end_word = end / BITS_PER_WORD;
		int j;
		UDWORD a = 0;
		UDWORD b = 0;
		for (i=0, j=start_word; j>=end_word; i++, j--) {
			if (i < N_WORDS(v->size)) {
				a |= ((UDWORD) (UWORD) word_reverse_bits(v->words[i].aval));
				b |= ((UDWORD) (UWORD) word_reverse_bits(v->words[i].bval));
			}
			if (j != start_word && j != end_word) {
				result->words[j].aval = a >> bit_shift;
				result->words[j].bval = b >> bit_shift;
			} else {
				UWORD mask = ~((UWORD) 0);
				if (j == end_word)
					mask &= ~((1 << (end % BITS_PER_WORD)) - 1);
				if (j == start_word)
					mask &= (1 << ((start + 1) % BITS_PER_WORD)) - 1;
				result->words[j].aval = (result->words[j].aval & ~mask) | ((a >> bit_shift) & mask);
				result->words[j].bval = (result->words[j].bval & ~mask) | ((b >> bit_shift) & mask);
			}
			a <<= BITS_PER_WORD;
			b <<= BITS_PER_WORD;
		}
	}
	vec4state_clear_padding_bits(result);
}

double vec4state_to_real (const struct vec4state *v)
{
	int sign = (v->is_signed && (vec4state_bit_test(v, v->size-1) == vpi1)) ? 1 : 0;
	int i, j;
	if (vec4state_any_bit_is_X_or_Z(v))
		return 0;
	for (i=v->size-1; i>=0; i--) {
		unsigned word = i / BITS_PER_WORD;
		unsigned bit = i % BITS_PER_WORD;
		if (((v->words[word].aval >> bit) & 1) != sign) {
			union real { double d; uint64_t u64; };
			const union real const_real_one = { .d = 1.0 };
			union real real = { .u64 = 0 };
			unsigned shift = 64 - 1 - (i % BITS_PER_WORD);
			for (j=0; j<64/BITS_PER_WORD; j++) {
				if (word < j || shift >= 64)
					break;
				real.u64 |= ((uint64_t) (UWORD) v->words[word - j].aval) << shift;
				shift -= BITS_PER_WORD;
			}
			if (sign != 0)
				real.u64 = -real.u64;
			/* strip MSB (implicit 1) */
			real.u64 <<= 1;
			/* Round away from zero (cf. 1364-2005, sec. 3.9.2): */
			real.u64 += 1 << (64 - 52 - 1);
			real.u64 >>= (64 - 52);
			/* exponent + sign */
			real.u64 |= ((uint64_t) (0x3ff + i) & 0x7ff) << 52;
			real.u64 |= ((uint64_t) sign) << 63;
			if (const_real_one.u64 == 0x3ff0000000000000ULL)
				/* FLOAT_WORD_ORDER == uint64_t-WORD_ORDER, this test should get optimized away. */ ;
			else if (const_real_one.u64 == 0x000000003ff00000ULL)
				real.u64 = (real.u64 << 32) | (real.u64 >> 32);
			else
				fatal("vec4state_to_real(): impossible double float word order?!\n");
			return real.d;
		}
	}
	return 0;
}

static inline size_t count_vpiBinOctHexStr_digits (const char *s)
{
	size_t len = 0;
	while (*s != '\0') {
		switch (*s) {
		case '0' ... '9':
		case 'a' ... 'f':
		case 'A' ... 'F':
		case 'x':
		case 'X':
		case 'z':
		case 'Z':
		case '?':
			len++;
			break;
		default: ;
		}
		s++;
	}
	return len;
}

static char * scan_size_and_base (char *s, PLI_INT32 *format, size_t *def_size, int *bits_per_digit, int *is_signed)
{
	char *digit_string = s;
	char base;
	if (s[0] < '0' || s[0] > '9') {
		*def_size = 0;
	} else {
		*def_size = strtoul(s, &digit_string, 10);
		if (*def_size == 0 || *def_size == ULONG_MAX) {
			if (*format != vpiDecStrVal && *format != vpiUndefined) {
				err("scan_size_and_base(): invalid size field (overflow or zero?)\n");
				return NULL;
			}
			/* overflow or zero size field -- assume vpiDecStrVal format without size */
			*format = vpiDecStrVal;
			*def_size = 0;
			*bits_per_digit = 4;
			*is_signed = 1;
			return s;
		}
		while (*digit_string == ' ' || *digit_string == '\t')
			digit_string++;
	}
	if (digit_string[0] != '\'') {
		if (*format != vpiDecStrVal && *format != vpiUndefined) {
			err("scan_size_and_base(): no base-'tick found!\n");
			return NULL;
		}
		/* no 'tick -- assume vpiDecStrVal format without size */
		*format = vpiDecStrVal;
		*def_size = 0;
		*bits_per_digit = 4;
		*is_signed = 1;
		return s;
	}
	digit_string++;
	if (digit_string[0] == 's' || digit_string[0] == 'S') {
		*is_signed = 1;
		digit_string++;
	} else {
		*is_signed = 0;
	}
	base = tolower(digit_string[0]);
	digit_string++;
	if (*format == vpiUndefined) {
		if (base == 'b') {
			*format = vpiBinStrVal;
		} else if (base == 'o') {
			*format = vpiOctStrVal;
		} else if (base == 'h') {
			*format = vpiHexStrVal;
		} else if (base == 'd') {
			*format = vpiDecStrVal;
		} else {
			err("scan_size_and_base(): invalid base character 0x%02x!\n", base);
			return NULL;
		}
	}
	if ((base == 'b' && *format != vpiBinStrVal) || (base == 'o' && *format != vpiOctStrVal) || (base == 'h' && *format != vpiHexStrVal) || (base == 'd' && *format != vpiDecStrVal)) {
		err("scan_size_and_base(): base character not matching format!\n");
		return NULL;
	}
	*bits_per_digit = (*format == vpiBinStrVal) ? 1 : (*format == vpiOctStrVal) ? 3 : 4;
	return digit_string;
}

struct vec4state * vec4state_from_vpiBinOctHexStr (char *s, size_t def_size, int bits_per_digit, int is_signed)
{
	struct vec4state *v;
	size_t n_digits;
	size_t v_size;
	int bit_idx;
	int i, j;
	n_digits = count_vpiBinOctHexStr_digits(s);
	v_size = n_digits * bits_per_digit;
	if (def_size != 0 && v_size > def_size) {
		err("vec4state_from_vpiBinOctHexStr(): too many digits for defined size (%u > %ubits)!\n", (unsigned) v_size, (unsigned) def_size);
		return NULL;
	}
	v = vec4state_new(v_size, is_signed);
	for (bit_idx=0, i=strlen(s)-1; i>=0; i--) {
		if (s[i] == '_')
			continue;
		if (s[i] == 'x' || s[i] == 'X') {
			for (j=0; j<bits_per_digit; j++)
				vec4state_bit_set(v, bit_idx++, vpiX);
		} else if (s[i] == 'z' || s[i] == 'Z' || s[i] == '?') {
			for (j=0; j<bits_per_digit; j++)
				vec4state_bit_set(v, bit_idx++, vpiZ);
		} else {
			int val = INT_MAX;
			if (s[i] >= '0' && s[i] <= '9')
				val = s[i] - '0';
			else if (s[i] >= 'a' && s[i] <= 'f')
				val = 0xa + s[i] - 'a';
			else if (s[i] >= 'A' && s[i] <= 'F')
				val = 0xA + s[i] - 'A';
			if (val >= (1 << bits_per_digit))
				err("vec4state_from_vpiBinOctHexStr(): Invalid character 0x%02x!\n", s[i] & 0xff);
			for (j=0; j<bits_per_digit; j++) {
				WORD bit = (val & (1 << j)) ? vpi1 : vpi0;
				vec4state_bit_set(v, bit_idx++, bit);
			}
		}
	}
	if (def_size == 0)
		def_size = v_size;
	if (!vec4state_resize(v, def_size))
		fatal("vec4state_resize failed!\n");
	return v;
}

struct vec4state * vec4state_from_vpiDecStr (char *s, size_t def_size, int is_signed)
{
	struct vec4state *v;
	int i;
	/* Conservative estimate of 4 bits per digit (real: log10(x)/log2(x)=3.321928). */
	v = vec4state_new(4 * strlen(s), is_signed);
	vec4state_set(v, vpi0);
	for (i=0; i<strlen(s); i++) {
		if (s[i] == '_')
			continue;
		if (s[i] >= '0' && s[i] <= '9') {
			WORD digit = s[i] - '0';
			vec4state_mul_unsigned_word(v, 10, v);
			vec4state_add_unsigned_word(v, digit, v);
		} else if (s[i] == 'x' || s[i] == 'X') {
			vec4state_set(v, vpiX);
			break;
		} else if (s[i] == 'z' || s[i] == 'Z' || s[i] == '?') {
			vec4state_set(v, vpiZ);
			break;
		} else {
			err("vec4state_from_vpiDecStr(): invalid character in decimal digit string: 0x%02x\n", s[i]);
			vec4state_free(v);
			return NULL;
		}
	}
	if (def_size != 0) {
		for (i=v->size-1; i>=def_size; i--) {
			if (vec4state_bit_test(v, i) != vpi0) {
				err("vec4state_from_vpiDecStr(): number '%s' too large to fit in %zd bits!\n", s, def_size);
				vec4state_free(v);
				return NULL;
			}
		}
		if (!vec4state_resize(v, def_size))
			fatal("vec4state_resize failed!\n");
	} else {
		int limit_size = 32;
		if (v->size >= 32) {
			for (i=v->size-1; i>=32; i--) {
				if (vec4state_bit_test(v, i) == vpi1) {
					warn("vec4state_from_vpiDecStr(): integer > 32bit (actually %d, use %ubits).\n"
					     "   IEEE1364-2005 is ambiguous about the wrap-around of unsized integers:\n"
					     "   In sec. 3.9 an implementation-dependend minimum size of 32bit is specified,\n"
					     "   but in sec. 4.1.3, the examples assume a fixed 32bit-wrap-around.\n"
					     "   We follow sec. 3.9 and won't limit integers to 32bits, to avoid unexpected wraparound effects.\n"
					     "   Please keep in mind that other Verilog Tools may expose different behaviour.\n", i, (unsigned) v->size);
					limit_size = v->size;
					break;
				}
			}
		}
		if (!vec4state_resize(v, limit_size))
			fatal("vec4state_resize failed!\n");
	}
	return v;
}

struct vec4state * vec4state_from_string (char *s, PLI_INT32 format)
{
	size_t def_size;
	int bits_per_digit;
	int is_signed;
	struct vec4state *v;
	if ((s = scan_size_and_base(s, &format, &def_size, &bits_per_digit, &is_signed)) == NULL) {
		err("vec4state_from_string(): Failed parsing size and base!\n");
		return NULL;
	}
	if (format == vpiDecStrVal)
		v = vec4state_from_vpiDecStr(s, def_size, is_signed);
	else
		v = vec4state_from_vpiBinOctHexStr(s, def_size, bits_per_digit, is_signed);
	return v;
}

char * vec4state_to_vpiBinOctHexStr (const struct vec4state *v, PLI_INT32 format)
{
	/* A 32-bit decimal number requires up to 10 digits, plus 4 (for tick ['], sign [sS], base [bBoOhH], '\0': */
	char size_and_base [14];
	char base = (format == vpiHexStrVal) ? 'h' : (format == vpiOctStrVal) ? 'o' : 'b';
	size_t n = snprintf(size_and_base, sizeof(size_and_base), "%d'%s%c", (int) v->size, v->is_signed ? "s" : "", base);
	int bits_per_digit = (format == vpiHexStrVal) ? 4 : (format == vpiOctStrVal) ? 3 : 1;
	size_t s_len = (v->size + bits_per_digit - 1) / bits_per_digit;
	char *s;
	int bit_idx = 0;
	int i, j;
	if (n >= sizeof(size_and_base) - 1) {
		err("vec4state_to_vpiBinOctHexStr(): overflow of size/base field!\n");
		return NULL;
	}
	if ((s = malloc(n + s_len + 1)) == NULL) {
		fatal("vec4state_to_vpiBinOctHexStr(): failed to allocate %lu bytes for output string!\n", (unsigned long) n + s_len + 1);
		return NULL;
	}
	strncpy(s, size_and_base, n);
	s[n + s_len] = '\0';
	for (i=n+s_len-1; i>=n; i--) {
		int number_of_X = 0;
		int number_of_Z = 0;
		int val = 0;
		for (j=0; j<bits_per_digit; j++) {
			PLI_INT32 bit = vec4state_bit_test(v, bit_idx);
			if (bit == vpi1)
				val |= 1 << j;
			else if (bit == vpiX)
				number_of_X++;
			else if (bit == vpiZ)
				number_of_Z++;
			if (++bit_idx >= v->size)
				break;
		}
		if (number_of_X == bits_per_digit)
			s[i] = 'x';
		else if (number_of_X != 0)
			s[i] = 'X';
		else if (number_of_Z == bits_per_digit)
			s[i] = 'z';
		else if (number_of_Z != 0)
			s[i] = 'Z';
		else if (val < 10)
			s[i] = '0' + val;
		else
			s[i] = 'a' + (val - 10);
	}
	return s;
}

char * vec4state_to_vpiDecStr (const struct vec4state *v)
{
	struct vec4state *v_unsigned = vec4state_new(v->size, 0);
	/* Conservative estimate of 0.5 digits per bit (real: log2(x)/log10(x)=0.301029996). Add 14 chars for size'base: */
	size_t s_size = 14 + v->size / 2 + 1;
	char *s = malloc(s_size);
	int s_len = 0;
	int digit;
	if (v->is_signed && (vec4state_bit_test(v, v->size - 1) == vpi1)) {
		vec4state_sub(&vec4state_const_zero, v, v_unsigned);
		s[s_len++] = '-';
	} else {
		vec4state_copy(v, v_unsigned);
	}
	/* For 32bit, signed decimals (default integer representation), we can omit size, sign and base char: */
	if (v->size != 32 || !v->is_signed)
		s_len += snprintf(&s[s_len], s_size - s_len, "%d'%sd", (int) v->size, v->is_signed ? "s" : "");
	if (vec4state_any_bit_is_X_or_Z(v)) {
		s[s_len++] = 'x';
	} else {
		int first_digit = s_len;
		int i;
		while (1) {
			PLI_INT32 c;
			digit = vec4state_div_unsigned_word(v_unsigned, 10, v_unsigned);
			s[s_len++] = '0' + digit;
			c = vec4state_cmp(v_unsigned, &vec4state_const_zero);
			if (c == vpi0 || c == vpiX || c == vpiZ)
				break;
		}
		for (i=0; i<(s_len-first_digit)/2; i++) {
			char tmp = s[first_digit+i];
			s[first_digit+i] = s[s_len-i-1];
			s[s_len-i-1] = tmp;
		}
	}
	s[s_len] = '\0';
	/* should never fail, since s_len is always smaller than our initial estimate: */
	s = realloc(s, s_len + 1);
	vec4state_free(v_unsigned);
	return s;
}

#define check_padding(v) __check_padding(#v, v)
static void __check_padding (const char *v_string, struct vec4state *v)
{
	if ((UWORD) v->words[N_WORDS(v->size)-1].aval & ~WORD_PADDING_MASK(v->size)) {
		fatal("padding error (a) (vec4state %s, bits 0x%08x, mask 0x%08x)!\n", v_string,
			v->words[N_WORDS(v->size)-1].aval, WORD_PADDING_MASK(v->size));
	}
	if ((UWORD) v->words[N_WORDS(v->size)-1].bval & ~WORD_PADDING_MASK(v->size)) {
		fatal("padding error (b) (vec4state %s, bits 0x%08x, mask 0x%08x)!\n", v_string,
			v->words[N_WORDS(v->size)-1].aval, WORD_PADDING_MASK(v->size));
	}
}

int main (int argc, char **argv)
{
	struct vec4state *a = vec4state_from_string(argv[1], vpiUndefined);
	struct vec4state *b = vec4state_from_string(argv[2], vpiUndefined);
	struct vec4state *three = vec4state_from_string("3", vpiUndefined);
	if (a && b && three) {
		char *a_b = vec4state_to_vpiBinOctHexStr(a, vpiBinStrVal);
		char *a_o = vec4state_to_vpiBinOctHexStr(a, vpiOctStrVal);
		char *a_h = vec4state_to_vpiBinOctHexStr(a, vpiHexStrVal);
		char *a_d = vec4state_to_vpiDecStr(a);
		char *b_b = vec4state_to_vpiBinOctHexStr(b, vpiBinStrVal);
		char *b_o = vec4state_to_vpiBinOctHexStr(b, vpiOctStrVal);
		char *b_h = vec4state_to_vpiBinOctHexStr(b, vpiHexStrVal);
		char *b_d = vec4state_to_vpiDecStr(b);
		check_padding(a);
		check_padding(b);
		check_padding(three);
		msg("a = %s\n", a_h);
		msg("b = %s\n", b_h);
		msg("a = %s\n", a_o);
		msg("b = %s\n", b_o);
		msg("a = %s\n", a_b);
		msg("b = %s\n", b_b);
		msg("a = %s\n", a_d);
		msg("b = %s\n", b_d);
		msg("a = %g\n", vec4state_to_real(a));
		msg("b = %g\n", vec4state_to_real(b));
		free(a_o);
		free(a_h);
		free(a_d);
		free(b_o);
		free(b_h);
		free(b_d);
		#define T3(x, size, is_signed)							\
			do {									\
				struct vec4state *c = vec4state_new(size, is_signed);		\
				char *c_b;							\
				char *c_d;							\
				msg(#x ":\n");							\
				x;								\
				c_b = vec4state_to_vpiBinOctHexStr(c, vpiBinStrVal);		\
				c_d = vec4state_to_vpiDecStr(c);				\
				msg("a = %s\n", a_b);						\
				msg("b = %s\n", b_b);						\
				msg("c = %s\n", c_b);						\
				msg("c = %s\n", c_d);						\
				check_padding(a);						\
				check_padding(b);						\
				check_padding(c);						\
				vec4state_free(c);						\
				free(c_b);							\
				free(c_d);							\
			} while (0)
		#define T4(x, size_c, size_d)							\
			do {									\
				struct vec4state *d = vec4state_new(size_d, 0);			\
				char *d_b;							\
				char *d_d;							\
				T3(x, size_c, 0);						\
				d_b = vec4state_to_vpiBinOctHexStr(d, vpiBinStrVal);		\
				d_d = vec4state_to_vpiDecStr(d);				\
				msg("d = %s\n", d_b);						\
				msg("d = %s\n", d_d);						\
				check_padding(d);						\
				vec4state_free(d);						\
				free(d_b);							\
				free(d_d);							\
			} while (0)
		T3(vec4state_shift_left(b, 5, c), b->size, b->is_signed);
		T3(vec4state_shift_right(b, 5, c, 0), b->size, b->is_signed);
		T3(vec4state_shift_right(b, 5, c, 1), b->size, b->is_signed);
		T3(vec4state_bitwise_binary_and(a, b, c), max(a->size, b->size), a->is_signed | b->is_signed);
		T3(vec4state_bitwise_binary_or(a, b, c), max(a->size, b->size), a->is_signed | b->is_signed);
		T3(vec4state_bitwise_binary_xor(a, b, c), max(a->size, b->size), a->is_signed | b->is_signed);
		T3(vec4state_bitwise_unary_negation(b, c), b->size, b->is_signed);
		T3(vec4state_add(a, b, c), max(a->size, b->size) + 1, a->is_signed | b->is_signed);
		T3(vec4state_sub(a, b, c), max(a->size, b->size) + 1, a->is_signed | b->is_signed);
		T3(vec4state_mul_unsigned_word(a, 10, c), a->size + BITS_PER_WORD, a->is_signed);
		T3(vec4state_mul(a, b, c), a->size + b->size, a->is_signed | b->is_signed);
		T3(vec4state_div_unsigned_word(b, 2, c), b->size, 0);
		T3(vec4state_reduction_unary_and(b, c), 1, 0);
		T3(vec4state_reduction_unary_or(b, c), 1, 0);
		T3(vec4state_reduction_unary_xor(b, c), 1, 0);
		T3(vec4state_bitselect_get(b, 0, c, 0), 8, b->is_signed);
		T3(vec4state_bitselect_get(b, 0, c, 1), 8, b->is_signed);
		T3(vec4state_bitselect_get(b, 1, c, 0), 35, b->is_signed);
		T3(vec4state_bitselect_get(b, 1, c, 1), 35, b->is_signed);
		T3(vec4state_bitselect_set(b, min(1, b->size), max(0, (int) b->size - 7), c), b->size, b->is_signed);
		T3(vec4state_bitselect_set(b, min(3, b->size), max(0, (int) b->size - 7), c), b->size, b->is_signed);
		T3(vec4state_bitselect_set(b, max(0, (int) b->size - 7), min(1, b->size), c), b->size, b->is_signed);
		T3(vec4state_bitselect_set(b, max(0, (int) b->size - 7), min(3, b->size), c), b->size, b->is_signed);
		T3(vec4state_pow(a, three, c), 3 * a->size, a->is_signed);
		T4(vec4state_div_unsigned(a, b, c, d), a->size, b->size);
		#undef T3
		#undef T4
		free(a_b);
		free(b_b);
	}
	vec4state_free(a);
	vec4state_free(b);
	vec4state_free(three);
	return 0;
}

