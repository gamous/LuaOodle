// Copyright Epic Games, Inc. All Rights Reserved.
// This source file is licensed solely to users who have
// accepted a valid Unreal Engine license agreement 
// (see e.g., https://www.unrealengine.com/eula), and use
// of this source file is governed by such agreement.

#include "cpux86.h"

#ifdef __RADX86__

#ifdef _MSC_VER
	#include <intrin.h>

	#if _MSC_VER >= 1500 // VC++2008 or later
	#define HAVE_CPUIDEX
	#endif
#elif 0 // defined(__RADANDROID__)
#include <cpuid.h>

#define __cpuid(level, a, b, c, d)			\
  __asm__ ("xchg{l}\t{%%}ebx, %k1\n\t"			\
	   "cpuid\n\t"					\
	   "xchg{l}\t{%%}ebx, %k1\n\t"			\
	   : "=a" (a), "=&r" (b), "=c" (c), "=d" (d)	\
	   : "0" (level))

#define __cpuid_count(level, count, a, b, c, d)		\
  __asm__ ("xchg{l}\t{%%}ebx, %k1\n\t"			\
	   "cpuid\n\t"					\
	   "xchg{l}\t{%%}ebx, %k1\n\t"			\
	   : "=a" (a), "=&r" (b), "=c" (c), "=d" (d)	\
	   : "0" (level), "2" (count))

#else
	// GCC/Clang
	#ifdef __RADX64__

	// 64-bit: GCC/Clang won't let us use "=b" constraint on Mac64, and we need to preserve RBX
	// (PIC/PIE base)
	#define __cpuidex(out, leaf_id, subleaf_id)\
		asm("xchgq %%rbx,%q1\n" \
			"cpuid\n" \
			"xchgq %%rbx,%q1\n" \
			: "=a" (out[0]), "=&r" (out[1]), "=c" (out[2]), "=d" (out[3]): "0" (leaf_id), "2"(subleaf_id));

	#else

	// 32-bit PIC-safe version

	#define __cpuidex(out, leaf_id, subleaf_id)\
		asm("xchgl %%ebx,%k1\n" \
			"cpuid\n" \
			"xchgl %%ebx,%k1\n" \
			: "=a" (out[0]), "=&r" (out[1]), "=c" (out[2]), "=d" (out[3]): "0" (leaf_id), "2"(subleaf_id));

	#endif // __RADX64__

	#define HAVE_CPUIDEX
	#define __cpuid(out, leaf_id) __cpuidex(out, leaf_id, 0)
#endif // _MSC_VER or not


OODLE_NS_START

#ifdef RRX86_CPU_DYNAMIC_DETECT
 
// note : g_rrCPUx86_feature_flags is a global atomic shared variable
//   we play lazy & loose with the thread safety here (atomics are in ext)
//	most likely it's fine
U32 g_rrCPUx86_feature_flags = 0;

void rrCPUx86_detect()
{
	int cpuid_info[4];
	U32 features = 0;
	U32 max_leaf;
	bool is_amd = false;

	// if we already detected, we're good!
	features = g_rrCPUx86_feature_flags; // atomic or volatile load?
	if (features & RRX86_CPU_INITIALIZED)
		return;

	// Basic CPUID information
	__cpuid(cpuid_info, 0);
	max_leaf = cpuid_info[0];

	// Is it AMD?
	if (cpuid_info[1] == 0x68747541 /* "Auth" */ && cpuid_info[3] == 0x69746e65 /* "enti" */ &&
		cpuid_info[2] == 0x444d4163 /* "cAMD" */)
	{
		is_amd = true;
	}

	// Basic feature flags
	__cpuid(cpuid_info, 1);

	if (cpuid_info[3] & (1u<<26))	features |= RRX86_CPU_SSE2;
	if (cpuid_info[2] & (1u<< 9))	features |= RRX86_CPU_SSSE3;
	if (cpuid_info[2] & (1u<<19))	features |= RRX86_CPU_SSE41;
	if (cpuid_info[2] & (1u<<20))	features |= RRX86_CPU_SSE42;
	if (cpuid_info[2] & (1u<<28))	features |= RRX86_CPU_AVX;
	if (cpuid_info[2] & (1u<<29))	features |= RRX86_CPU_F16C;

	if (is_amd)
	{
		U32 family = (cpuid_info[0] >> 8) & 0xf;
		U32 ext_family = (cpuid_info[0] >> 20) & 0xff;

		// Zen aka AMD 17h has family=0xf, ext_family=0x08 (Zen and Zen2 both)
		// Zen3 aka AMD 19h has family=0xf, ext_family=0x0a
		// so just testing for this:
		if (family == 0xf && ext_family >= 0x08)
			features |= RRX86_CPU_AMD_ZEN;
	}

#ifdef HAVE_CPUIDEX
	if (max_leaf >= 7)
	{
		// "Structured extended feature flags enumeration"
		__cpuidex(cpuid_info, 7, 0);

		// Some (Celeron) Skylakes erroneously report BMI1/BMI2 even though they don't have it.
		// These Celerons also don't have AVX.
		//
		// All CPUs that actually have BMI1/BMI2 (as of this writing, 2016-05-11) have AVX.
		// (The ones we care about, anyway.) So only report BMI1/BMI2 if AVX is present.
		if (features & RRX86_CPU_AVX)
		{
			if (cpuid_info[1] & (1u<< 3))	features |= RRX86_CPU_BMI1;
			if (cpuid_info[1] & (1u<< 8))	features |= RRX86_CPU_BMI2;
			if (cpuid_info[1] & (1u<< 5))	features |= RRX86_CPU_AVX2;
		}
	}
#endif

	// Super-paranoia: we use the AMD_ZEN flag to indicate we are free to use Zen-optimized
	// kernels without further CPUID checks. In case some joker monekys around with with CPUID
	// flags in the future, turn it off again if we don't have the CPUID bits we should have
	// on a real Zen.
	if (features & RRX86_CPU_AMD_ZEN)
	{
		const U32 zen_features = RRX86_CPU_SSE2 | RRX86_CPU_SSSE3 | RRX86_CPU_SSE41 | RRX86_CPU_SSE42 | RRX86_CPU_F16C |
			RRX86_CPU_AVX | RRX86_CPU_AVX2 |
			RRX86_CPU_BMI1 | RRX86_CPU_BMI2;

		if ((features & zen_features) != zen_features)
			features &= ~RRX86_CPU_AMD_ZEN;
	}

	// write detected features
	// only write value once at end of the function!
	features |= RRX86_CPU_INITIALIZED;

	g_rrCPUx86_feature_flags = features; // atomic or volatile store
}

#endif // RRX86_CPU_DYNAMIC_DETECT

void rrCPUx86_flush_with_cpuid()
{
	int cpuid_info[4];
	__cpuid(cpuid_info, 0);
}

OODLE_NS_END

#endif  // __RADX86__

