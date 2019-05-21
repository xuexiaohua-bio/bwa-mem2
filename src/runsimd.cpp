#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define SIMD_SSE     0x1
#define SIMD_SSE2    0x2
#define SIMD_SSE3    0x4
#define SIMD_SSSE3   0x8
#define SIMD_SSE4_1  0x10
#define SIMD_SSE4_2  0x20
#define SIMD_AVX     0x40
#define SIMD_AVX2    0x80
#define SIMD_AVX512F 0x100
#define SIMD_AVX512BW 0x200

#ifndef _MSC_VER
// adapted from https://github.com/01org/linux-sgx/blob/master/common/inc/internal/linux/cpuid_gnu.h
void __cpuidex(int cpuid[4], int func_id, int subfunc_id)
{
#if defined(__x86_64__)
	__asm__ volatile ("cpuid"
			: "=a" (cpuid[0]), "=b" (cpuid[1]), "=c" (cpuid[2]), "=d" (cpuid[3])
			: "0" (func_id), "2" (subfunc_id));
#else // on 32bit, ebx can NOT be used as PIC code
	__asm__ volatile ("xchgl %%ebx, %1; cpuid; xchgl %%ebx, %1"
			: "=a" (cpuid[0]), "=r" (cpuid[1]), "=c" (cpuid[2]), "=d" (cpuid[3])
			: "0" (func_id), "2" (subfunc_id));
#endif
}
#endif

static int x86_simd(void)
{
	int flag = 0, cpuid[4], max_id;
	__cpuidex(cpuid, 0, 0);
	max_id = cpuid[0];
	if (max_id == 0) return 0;
	__cpuidex(cpuid, 1, 0);
	if (cpuid[3]>>25&1) flag |= SIMD_SSE;
	if (cpuid[3]>>26&1) flag |= SIMD_SSE2;
	if (cpuid[2]>>0 &1) flag |= SIMD_SSE3;
	if (cpuid[2]>>9 &1) flag |= SIMD_SSSE3;
	if (cpuid[2]>>19&1) flag |= SIMD_SSE4_1;
	if (cpuid[2]>>20&1) flag |= SIMD_SSE4_2;
	if (cpuid[2]>>28&1) flag |= SIMD_AVX;
	if (max_id >= 7) {
		__cpuidex(cpuid, 7, 0);
		if (cpuid[1]>>5 &1) flag |= SIMD_AVX2;
		if (cpuid[1]>>16&1) flag |= SIMD_AVX512F;
		if (cpuid[1]>>30&1) flag |= SIMD_AVX512BW;
	}
	return flag;
}

static int exe_path(const char *exe, int max, char buf[], int *base_st)
{
	int i, len, last_slash, ret = 0;
	if (exe == 0 || max == 0) return -1;
	buf[0] = 0;
	len = strlen(exe);
	for (i = len - 1; i >= 0; --i)
		if (exe[i] == '/') break;
	last_slash = i;
	if (base_st) *base_st = last_slash + 1;
	if (exe[0] == '/') {
		if (max < last_slash + 2) return -1;
		strncpy(buf, exe, last_slash + 1);
		buf[last_slash + 1] = 0;
	} else if (last_slash >= 0) { // actually, can't be 0
		char *p;
		int abs_len;
		p = getcwd(buf, max);
		if (p == 0) return -1;
		abs_len = strlen(buf);
		if (max < abs_len + 3 + last_slash) return -1;
		buf[abs_len] = '/';
		strncpy(buf + abs_len + 1, exe, last_slash + 1);
		buf[abs_len + last_slash + 2] = 0;
	} else {
		char *env, *p, *q, *tmp;
		int env_len, found = 0, ret;
		struct stat st;
		env = getenv("PATH");
		env_len = strlen(env);
		tmp = (char*)malloc(env_len + len + 2);
		for (p = q = env;; ++p) {
			if (*p == ':' || *p == 0) {
				strncpy(tmp, q, p - q);
				tmp[p - q] = '/';
				strcpy(tmp + (p - q + 1), exe);
				if (stat(tmp, &st) == 0 && (st.st_mode & S_IXUSR)) {
					found = 1;
					break;
				}
				if (*p == 0) break;
				q = p + 1;
			}
		}
		if (!found) {
			free(tmp);
			return -2; // shouldn't happen!
		}
		ret = exe_path(tmp, max, buf, 0);
		free(tmp);
	}
	return ret;
}

static void test_and_launch(char *argv[], int prefix_len, char *prefix, const char *simd) // we assume prefix is long enough
{
	struct stat st;
	strcpy(prefix + prefix_len, simd);
	if (stat(prefix, &st) == 0 && (st.st_mode & S_IXUSR)) {
		//fprintf(stderr, "Launching executable \"%s\"\n", prefix);
		execv(prefix, argv);
	}
}

int main(int argc, char *argv[])
{
	char buf[1024], *prefix, *argv0 = argv[0];
	int ret, buf_len, prefix_len, base_st, simd;
	ret = exe_path(argv0, 1024, buf, &base_st);
	if (ret != 0) {
		fprintf(stderr, "ERROR: prefix is too long!\n");
		return 1;
	}
	//printf("%s\n", buf);
	buf_len = strlen(buf);
	prefix = (char*)malloc(buf_len + (strlen(argv0) - base_st) + 20);
	strcpy(prefix, buf);
	strcpy(prefix + buf_len, &argv0[base_st]);
	prefix_len = strlen(prefix);
	simd = x86_simd();
	if (simd & SIMD_AVX512BW) test_and_launch(argv, prefix_len, prefix, ".avx512bw");
	if (simd & SIMD_AVX512F) test_and_launch(argv, prefix_len, prefix, ".avx512f");
	if (simd & SIMD_AVX2) test_and_launch(argv, prefix_len, prefix, ".avx2");
	if (simd & SIMD_AVX) test_and_launch(argv, prefix_len, prefix, ".avx");
	if (simd & SIMD_SSE4_2) test_and_launch(argv, prefix_len, prefix, ".sse42");
	if (simd & SIMD_SSE4_1) test_and_launch(argv, prefix_len, prefix, ".sse41");
	if (simd & SIMD_SSE2) test_and_launch(argv, prefix_len, prefix, ".sse2");
	if (simd & SIMD_SSE) test_and_launch(argv, prefix_len, prefix, ".sse");
	free(prefix);
	fprintf(stderr, "ERROR: fail to find the right executable\n");
	return 2;
}