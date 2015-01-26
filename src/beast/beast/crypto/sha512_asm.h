#ifndef __SHA512_ASM_INC__

#ifdef __cplusplus
extern "C" {
#endif

void sha512_sse4(const void* M, void* D, size_t L);
void sha512_avx(const void* M, void* D, size_t L);
void sha512_rorx(const void* M, void* D, size_t L);

#ifdef __cplusplus
}
#endif

#endif //__SHA512_ASM_INC__

