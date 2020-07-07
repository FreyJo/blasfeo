/**************************************************************************************************
*                                                                                                 *
* This file is part of BLASFEO.                                                                   *
*                                                                                                 *
* BLASFEO -- BLAS for embedded optimization.                                                      *
* Copyright (C) 2019 by Gianluca Frison.                                                          *
* Developed at IMTEK (University of Freiburg) under the supervision of Moritz Diehl.              *
* All rights reserved.                                                                            *
*                                                                                                 *
* The 2-Clause BSD License                                                                        *
*                                                                                                 *
* Redistribution and use in source and binary forms, with or without                              *
* modification, are permitted provided that the following conditions are met:                     *
*                                                                                                 *
* 1. Redistributions of source code must retain the above copyright notice, this                  *
*    list of conditions and the following disclaimer.                                             *
* 2. Redistributions in binary form must reproduce the above copyright notice,                    *
*    this list of conditions and the following disclaimer in the documentation                    *
*    and/or other materials provided with the distribution.                                       *
*                                                                                                 *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND                 *
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED                   *
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE                          *
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR                 *
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES                  *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;                    *
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND                     *
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT                      *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS                   *
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                    *
*                                                                                                 *
* Author: Gianluca Frison, gianluca.frison (at) imtek.uni-freiburg.de                             *
*                                                                                                 *
**************************************************************************************************/

#include <stdlib.h>
#include <stdio.h>

#include <blasfeo_target.h>
#include <blasfeo_block_size.h>
#include <blasfeo_common.h>
#include <blasfeo_d_aux.h>
#include <blasfeo_d_kernel.h>



// TODO move to a header file to reuse across routines
#define EL_SIZE 8 // double precision
#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_ARMV8A_ARM_CORTEX_A53)
#define M_KERNEL 12 // max kernel: 12x4
#define L1_CACHE_EL (32*1024/EL_SIZE) // L1 data cache size: 32 kB
#define CACHE_LINE_EL (64/EL_SIZE) // data cache size: 64 bytes
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE) | defined(TARGET_ARMV8A_ARM_CORTEX_A57)
#define M_KERNEL 8 // max kernel: 8x4
#define L1_CACHE_EL (32*1024/EL_SIZE) // L1 data cache size: 32 kB
#define CACHE_LINE_EL (64/EL_SIZE) // data cache size: 64 bytes
#else // assume generic target
#define M_KERNEL 4 // max kernel: 4x4
#define L1_CACHE_EL (32*1024/EL_SIZE) // L1 data cache size: 32 kB
#define CACHE_LINE_EL (64/EL_SIZE) // data cache size: 64 bytes // TODO 32-bytes for cortex A9
#endif



void blasfeo_hp_dpotrf_l(int m, struct blasfeo_dmat *sC, int ci, int cj, struct blasfeo_dmat *sD, int di, int dj)
	{

#if defined(PRINT_NAME)
	printf("\nblasfeo_hp_dpotrf_l (cm) %d %p %d %d %p %d %d\n", m, sC, ci, cj, sD, di, dj);
#endif

	if(m<=0)
		return;

	// extract pointer to column-major matrices from structures
	int ldc = sC->m;
	int ldd = sD->m;
	double *C = sC->pA + ci + cj*ldc;
	double *D = sD->pA + di + dj*ldd;

//	printf("\n%p %d %p %d\n", C, ldc, D, ldd);

	int ii, jj;

	const int ps = 4; //D_PS;

#if defined(TARGET_GENERIC)
	double pU[4*K_MAX_STACK];
	double dU[K_MAX_STACK];
#else
	ALIGNED( double pU[M_KERNEL*K_MAX_STACK], 64 );
	ALIGNED( double dU[K_MAX_STACK], 64 );
#endif
	int sdu = (m+3)/4*4;
	sdu = sdu<K_MAX_STACK ? sdu : K_MAX_STACK;

	struct blasfeo_pm_dmat tA;
	int sda;
	double *dA;
	int tA_size;
	void *mem;
	char *mem_align;
	int m1, n1, k1;
	int pack_B;

	const int m_kernel = M_KERNEL;
	const int l1_cache_el = L1_CACHE_EL;
	const int reals_per_cache_line = CACHE_LINE_EL;

	const int m_cache = (m+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
//	const int n_cache = (n+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
//	const int k_cache = (k+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int m_kernel_cache = (m_kernel+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	int m_min = m_cache<m_kernel_cache ? m_cache : m_kernel_cache;
//	int n_min = n_cache<m_kernel_cache ? n_cache : m_kernel_cache;


	double d_1 = 1.0;


//	goto l_0;
//	goto l_1;
#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=200 | m>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | m>K_MAX_STACK)
#else
	if(m>=12 | m>K_MAX_STACK)
#endif
		{
		goto l_1;
		}
	else
		{
		goto l_0;
		}

	// never to get here
	return;


l_0:

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<m-11; ii+=12)
		{
		for(jj=0; jj<ii; jj+=4)
			{
			kernel_dtrsm_nt_rl_inv_12x4_lib4cccc(jj, pU, sdu, D+jj, ldd, &d_1, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dU+jj);
			kernel_dpack_nn_12_lib4(4, D+ii+jj*ldd, ldd, pU+jj*ps, sdu);
			}
		kernel_dpotrf_nt_l_12x4_lib44cc(jj, pU, sdu, pU, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dU+jj);
		kernel_dpack_nn_8_lib4(4, D+ii+4+jj*ldd, ldd, pU+4*sdu+jj*ps, sdu);
		kernel_dpotrf_nt_l_8x8_lib44cc(jj+4, pU+4*sdu, sdu, pU+4*sdu, sdu, C+ii+4+(jj+4)*ldc, ldc, D+ii+4+(jj+4)*ldd, ldd, dU+jj+4);
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto l_0_left_4;
			}
		if(m-ii<=8)
			{
			goto l_0_left_8;
			}
		else
			{
			goto l_0_left_12;
			}
		}
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<m-7; ii+=8)
		{
		for(jj=0; jj<ii; jj+=4)
			{
			kernel_dtrsm_nt_rl_inv_8x4_lib4cccc(jj, pU, sdu, D+jj, ldd, &d_1, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dU+jj);
			kernel_dpack_nn_8_lib4(4, D+ii+jj*ldd, ldd, pU+jj*ps, sdu);
			}
		kernel_dpotrf_nt_l_8x4_lib44cc(jj, pU, sdu, pU, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dU+jj);
		kernel_dpack_nn_4_lib4(4, D+ii+4+jj*ldd, ldd, pU+4*sdu+jj*ps);
		kernel_dpotrf_nt_l_4x4_lib44cc(jj+4, pU+4*sdu, pU+4*sdu, C+ii+4+(jj+4)*ldc, ldc, D+ii+4+(jj+4)*ldd, ldd, dU+jj+4);
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto l_0_left_4;
			}
		else
			{
			goto l_0_left_8;
			}
		}
#else
	for(; ii<m-3; ii+=4)
		{
		for(jj=0; jj<ii; jj+=4)
			{
			kernel_dtrsm_nt_rl_inv_4x4_lib4cccc(jj, pU, D+jj, ldd, &d_1, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dU+jj);
			kernel_dpack_nn_4_lib4(4, D+ii+jj*ldd, ldd, pU+jj*ps);
			}
		kernel_dpotrf_nt_l_4x4_lib44cc(jj, pU, pU, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dU+jj);
		}
	if(ii<m)
		{
		goto l_0_left_4;
		}
#endif
	goto l_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
l_0_left_12:
	for(jj=0; jj<ii; jj+=4)
		{
		kernel_dtrsm_nt_rl_inv_12x4_vs_lib4cccc(jj, pU, sdu, D+jj, ldd, &d_1, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dU+jj, m-ii, ii-jj);
		kernel_dpack_nn_12_vs_lib4(4, D+ii+jj*ldd, ldd, pU+jj*ps, sdu, m-ii);
		}
	kernel_dpotrf_nt_l_12x4_vs_lib44cc(jj, pU, sdu, pU, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dU+jj, m-ii, m-jj);
	kernel_dpack_nn_8_vs_lib4(4, D+ii+4+jj*ldd, ldd, pU+4*sdu+jj*ps, sdu, m-ii-4);
	kernel_dpotrf_nt_l_8x8_vs_lib44cc(jj+4, pU+4*sdu, sdu, pU+4*sdu, sdu, C+ii+4+(jj+4)*ldc, ldc, D+ii+4+(jj+4)*ldd, ldd, dU+jj+4, m-ii-4, m-ii-4);
	goto l_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_X64_INTEL_SANDY_BRIDGE)
l_0_left_8:
	for(jj=0; jj<ii; jj+=4)
		{
		kernel_dtrsm_nt_rl_inv_8x4_vs_lib4cccc(jj, pU, sdu, D+jj, ldd, &d_1, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dU+jj, m-ii, ii-jj);
		kernel_dpack_nn_8_vs_lib4(4, D+ii+jj*ldd, ldd, pU+jj*ps, sdu, m-ii);
		}
	kernel_dpotrf_nt_l_8x4_vs_lib44cc(jj, pU, sdu, pU, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dU+jj, m-ii, m-jj);
	kernel_dpack_nn_4_vs_lib4(4, D+ii+4+jj*ldd, ldd, pU+4*sdu+jj*ps, m-ii-4);
	kernel_dpotrf_nt_l_4x4_vs_lib44cc(jj+4, pU+4*sdu, pU+4*sdu, C+ii+4+(jj+4)*ldc, ldc, D+ii+4+(jj+4)*ldd, ldd, dU+jj+4, m-ii-4, m-jj-4);
	goto l_0_return;
#endif

l_0_left_4:
	for(jj=0; jj<ii; jj+=4)
		{
		kernel_dtrsm_nt_rl_inv_4x4_vs_lib4cccc(jj, pU, D+jj, ldd, &d_1, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dU+jj, m-ii, ii-jj);
		kernel_dpack_nn_4_vs_lib4(4, D+ii+jj*ldd, ldd, pU+jj*ps, m-ii);
		}
	kernel_dpotrf_nt_l_4x4_vs_lib44cc(jj, pU, pU, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dU+jj, m-ii, m-jj);
	goto l_0_return;

l_0_return:
// TODO move to BLAS API !!!!!!!!!!!!!!!!!!!!!!!!!!!
//	for(ii=0; ii<m; ii++)
//		{
//		if(dU[ii]==0.0)
//			{
//			*info = ii+1;
//			return;
//			}
//		}
	return;


l_1:
	
	m1 = (m+128-1)/128*128;
	tA_size = blasfeo_pm_memsize_dmat(ps, m1, m1);
//	tA_size = blasfeo_memsize_dmat(m, m);
	mem = malloc(tA_size+64);
	blasfeo_align_64_byte(mem, (void **) &mem_align);
	blasfeo_pm_create_dmat(ps, m, m, &tA, (void *) mem_align);

	sda = tA.cn;
	dA = tA.dA;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_ARMV8A_ARM_CORTEX_A53)
	for(; ii<m-11; ii+=12)
		{
		jj = 0;
		for(; jj<ii; jj+=4)
			{
			kernel_dtrsm_nt_rl_inv_12x4_lib44ccc(jj, tA.pA+ii*sda, sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dA+jj);
			kernel_dpack_nn_12_lib4(4, D+ii+jj*ldd, ldd, tA.pA+ii*sda+jj*ps, sda);
			}
		kernel_dpotrf_nt_l_12x4_lib44cc(jj, tA.pA+ii*sda, sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dA+jj);
		kernel_dpack_nn_8_lib4(4, D+ii+4+jj*ldd, ldd, tA.pA+(ii+4)*sda+jj*ps, sda);
#if defined(TARGET_X64_INTEL_HASWELL)
		kernel_dpotrf_nt_l_8x8_lib44cc(jj+4, tA.pA+(ii+4)*sda, sda, tA.pA+(jj+4)*sda, sda, C+ii+4+(jj+4)*ldc, ldc, D+ii+4+(jj+4)*ldd, ldd, dA+jj+4);
		kernel_dpack_nn_4_lib4(4, D+ii+8+(jj+4)*ldd, ldd, tA.pA+(ii+8)*sda+(jj+4)*ps);
#else
		kernel_dpotrf_nt_l_8x4_lib44cc(jj+4, tA.pA+(ii+4)*sda, sda, tA.pA+(jj+4)*sda, C+ii+4+(jj+4)*ldc, ldc, D+ii+4+(jj+4)*ldd, ldd, dA+jj+4);
		kernel_dpack_nn_4_lib4(4, D+ii+8+(jj+4)*ldd, ldd, tA.pA+(ii+8)*sda+(jj+4)*ps);
		kernel_dpotrf_nt_l_4x4_lib44cc(jj+8, tA.pA+(ii+8)*sda, tA.pA+(jj+8)*sda, C+ii+8+(jj+8)*ldc, ldc, D+ii+8+(jj+8)*ldd, ldd, dA+jj+8);
#endif
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto l_1_left_4;
			}
		if(m-ii<=8)
			{
			goto l_1_left_8;
			}
		else
			{
			goto l_1_left_12;
			}
		}
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE) | defined(TARGET_ARMV8A_ARM_CORTEX_A57)
	for(; ii<m-7; ii+=8)
		{
		jj = 0;
		for(; jj<ii; jj+=4)
			{
			kernel_dtrsm_nt_rl_inv_8x4_lib44ccc(jj, tA.pA+ii*sda, sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dA+jj);
			kernel_dpack_nn_8_lib4(4, D+ii+jj*ldd, ldd, tA.pA+ii*sda+jj*ps, sda);
			}
		kernel_dpotrf_nt_l_8x4_lib44cc(jj, tA.pA+ii*sda, sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dA+jj);
		kernel_dpack_nn_4_lib4(4, D+ii+4+jj*ldd, ldd, tA.pA+(ii+4)*sda+jj*ps);
		kernel_dpotrf_nt_l_4x4_lib44cc(jj+4, tA.pA+(ii+4)*sda, tA.pA+(jj+4)*sda, C+ii+4+(jj+4)*ldc, ldc, D+ii+4+(jj+4)*ldd, ldd, dA+jj+4);
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto l_1_left_4;
			}
		else
			{
			goto l_1_left_8;
			}
		}
#else
	for(; ii<m-3; ii+=4)
		{
		jj = 0;
		for(; jj<ii; jj+=4)
			{
			kernel_dtrsm_nt_rl_inv_4x4_lib44ccc(jj, tA.pA+ii*sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dA+jj);
			kernel_dpack_nn_4_lib4(4, D+ii+jj*ldd, ldd, tA.pA+ii*sda+jj*ps);
			}
		kernel_dpotrf_nt_l_4x4_lib44cc(jj, tA.pA+ii*sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dA+jj);
		}
	if(ii<m)
		{
		goto l_1_left_4;
		}
#endif
	goto l_1_return;

#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_ARMV8A_ARM_CORTEX_A53)
l_1_left_12:
	for(jj=0; jj<ii; jj+=4)
		{
		kernel_dtrsm_nt_rl_inv_12x4_vs_lib44ccc(jj, tA.pA+ii*sda, sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dA+jj, m-ii, ii-jj);
		kernel_dpack_nn_12_vs_lib4(4, D+ii+jj*ldd, ldd, tA.pA+ii*sda+jj*ps, sda, m-ii);
		}
	kernel_dpotrf_nt_l_12x4_vs_lib44cc(jj, tA.pA+ii*sda, sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dA+jj, m-ii, m-jj);
	kernel_dpack_nn_8_vs_lib4(4, D+ii+4+jj*ldd, ldd, tA.pA+(ii+4)*sda+jj*ps, sda, m-ii-4);
#if defined(TARGET_X64_INTEL_HASWELL)
	kernel_dpotrf_nt_l_8x8_vs_lib44cc(jj+4, tA.pA+(ii+4)*sda, sda, tA.pA+(jj+4)*sda, sda, C+ii+4+(jj+4)*ldc, ldc, D+ii+4+(jj+4)*ldd, ldd, dA+jj+4, m-ii-4, m-jj-4);
#else
	kernel_dpotrf_nt_l_8x4_vs_lib44cc(jj+4, tA.pA+(ii+4)*sda, sda, tA.pA+(jj+4)*sda, C+ii+4+(jj+4)*ldc, ldc, D+ii+4+(jj+4)*ldd, ldd, dA+jj+4, m-(ii+4), m-(jj+4));
	kernel_dpack_nn_4_vs_lib4(8, D+ii+8+(jj+4)*ldd, ldd, tA.pA+(ii+8)*sda+(jj+4)*ps, m-ii-8);
	kernel_dpotrf_nt_l_4x4_vs_lib44cc(jj+8, tA.pA+(ii+8)*sda, tA.pA+(jj+8)*sda, C+ii+8+(jj+8)*ldc, ldc, D+ii+8+(jj+8)*ldd, ldd, dA+jj+8, m-ii-8, m-jj-8);
#endif
	goto l_1_return;
#endif


#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_X64_INTEL_SANDY_BRIDGE) | defined(TARGET_ARMV8A_ARM_CORTEX_A57) | defined(TARGET_ARMV8A_ARM_CORTEX_A53)
l_1_left_8:
	for(jj=0; jj<ii; jj+=4)
		{
		kernel_dtrsm_nt_rl_inv_8x4_vs_lib44ccc(jj, tA.pA+ii*sda, sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dA+jj, m-ii, ii-jj);
		kernel_dpack_nn_8_vs_lib4(4, D+ii+jj*ldd, ldd, tA.pA+ii*sda+jj*ps, sda, m-ii);
		}
	kernel_dpotrf_nt_l_8x4_vs_lib44cc(jj, tA.pA+ii*sda, sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dA+jj, m-ii, m-jj);
	kernel_dpack_nn_4_vs_lib4(4, D+ii+4+jj*ldd, ldd, tA.pA+(ii+4)*sda+jj*ps, m-ii-4);
	kernel_dpotrf_nt_l_4x4_vs_lib44cc(jj+4, tA.pA+(ii+4)*sda, tA.pA+(jj+4)*sda, C+ii+4+(jj+4)*ldc, ldc, D+ii+4+(jj+4)*ldd, ldd, dA+jj+4, m-ii-4, m-jj-4);
	goto l_1_return;
#endif

l_1_left_4:
	for(jj=0; jj<ii; jj+=4)
		{
		kernel_dtrsm_nt_rl_inv_4x4_vs_lib44ccc(jj, tA.pA+ii*sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, D+jj+jj*ldd, ldd, dA+jj, m-ii, ii-jj);
		kernel_dpack_nn_4_vs_lib4(4, D+ii+jj*ldd, ldd, tA.pA+ii*sda+jj*ps, m-ii);
		}
	kernel_dpotrf_nt_l_4x4_vs_lib44cc(jj, tA.pA+ii*sda, tA.pA+jj*sda, C+ii+jj*ldc, ldc, D+ii+jj*ldd, ldd, dA+jj, m-ii, m-jj);
	goto l_1_return;

l_1_return:
// TODO move to BLAS API !!!!!!!!!!!!!!!!!!!!!!!!!!!
//	for(ii=0; ii<m; ii++)
//		{
//		if(dA[ii]==0.0)
//			{
//			*info = ii+1;
//			free(mem);
//			return;
//			}
//		}
	free(mem);
	return;


	// never to get here
	return;

	}



#if defined(LA_HIGH_PERFORMANCE)



void blasfeo_dpotrf_l(int m, struct blasfeo_dmat *sC, int ci, int cj, struct blasfeo_dmat *sD, int di, int dj)
	{
	blasfeo_hp_dpotrf_l(m, sC, ci, cj, sD, di, dj);
	}



#endif

