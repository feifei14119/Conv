﻿#pragma once

#include "ConvFwd1x1KernelWriter.h"

using namespace TensileConv;
using namespace AutoGen;

KernelWriterConv1x1::KernelWriterConv1x1(T_Conv1x1KernelParam kernelParam, E_IsaArch isaArch)
	:KernelWriter(isaArch)
{
	kernelName = "ConvFwd1x1";

	// 复制参数, 方便使用
	this->kernelParam = kernelParam;
	N = kernelParam.N;
	C = kernelParam.C; K = kernelParam.K;
	W = kernelParam.W; H = kernelParam.H;
	EnBias = kernelParam.EnBias;
	Relu = kernelParam.Relu;

	c_in_lds_split_group = kernelParam.c_in_lds_split_group;
	c_in_lds_atomic_group = kernelParam.c_in_lds_atomic_group;
	c_in_l2_split_group = kernelParam.c_in_l2_split_group;
	c_in_l2_atomic_group = kernelParam.c_in_l2_atomic_group;
	k_out_maps = kernelParam.k_out_maps;
	group_sz.x = kernelParam.group_size_x;
	PCK_order = kernelParam.PCK_order;
}
E_ReturnState KernelWriterConv1x1::checkKernelParam()
{
#if KERNEL_DEBUG
	print_kernel_param();
#endif

	// -------------------------------------------------------------------------------
	// 问题尺寸过滤
	// -------------------------------------------------------------------------------
	if ((K % k_out_maps) != 0) return E_ReturnState::RTN_ERR;

	// -------------------------------------------------------------------------------
	// 中间变量计算
	// -------------------------------------------------------------------------------
	in_chan_stride = W * H;
	in_batch_stride = W * H * C;
	wei_chan_stride = C;
	out_chan_stride = W * H;
	out_batch_stride = W * H * K;
	out_size = W * H * K * N;

	// -------------------------------------------------------------------------------
	// input channel
	// -------------------------------------------------------------------------------
	c_in_lds_group = c_in_lds_split_group * c_in_lds_atomic_group;
	c_in_l2_group = c_in_l2_split_group * c_in_l2_atomic_group;
	c_in_group = c_in_l2_group * c_in_lds_group;
	c_in_maps = C / c_in_group;
	if (c_in_maps < unroll_time)
		return E_ReturnState::RTN_ERR;
	if ((C % c_in_group) != 0)
		return E_ReturnState::RTN_ERR;
	if (c_in_maps_once <= c_in_maps / unroll_time)
	{
		c_in_maps_once_real = 2;
		while (c_in_maps_once_real <= c_in_maps_once)
		{
			if (c_in_maps_once_real >= 8)// 不超过原始最大值
			{
				break;
			}
			if (((c_in_maps / unroll_time) % c_in_maps_once_real) != 0)
			{
				c_in_maps_once_real /= 2;
				break;
			}
			c_in_maps_once_real *= 2;
		}
	}
	else
	{
		c_in_maps_once_real = c_in_maps / unroll_time;
	}
	if (c_in_maps_once_real <= 0)
		return E_ReturnState::RTN_ERR;
	conv_loop = c_in_maps / c_in_maps_once_real / unroll_time;
	if (c_in_maps != conv_loop * c_in_maps_once_real * unroll_time)
		return E_ReturnState::RTN_ERR;
	if (c_in_maps_once_real != 1 && c_in_maps_once_real != 2 &&
		c_in_maps_once_real != 4 && c_in_maps_once_real != 8 && c_in_maps_once_real != 16)
		return E_ReturnState::RTN_ERR;

	// -------------------------------------------------------------------------------
	// others
	// -------------------------------------------------------------------------------
	k_out_group = divCeil(K, k_out_maps);
	pix_group = divCeil(W * H * N, group_sz.x);
	pix_wave = group_sz.x / WAVE_SIZE * pix_group;
	align = pix_group * group_sz.x;

	en_l2_sync = ((Relu == RELU && c_in_l2_atomic_group > 1) || (c_in_l2_split_group > 1));
	en_input_offset = ((IsaArch == E_IsaArch::Gfx900) && (W*H * 4 <= 4095));
	en_wei_addr_offset = true;

	// -------------------------------------------------------------------------------
	// memory size
	// -------------------------------------------------------------------------------
	size_sig = size_l2 = size_dbg = 0;
	if (en_l2_sync)
		size_sig = pix_wave * k_out_group;
	if (c_in_l2_split_group > 1)
		size_l2 = out_size * c_in_l2_split_group;
#if KERNEL_DEBUG
	size_dbg = align * c_in_group * k_out_group;	// total thread number, 2D
#endif

	// -------------------------------------------------------------------------------
	// work load
	// -------------------------------------------------------------------------------
	global_sz.x = align * c_in_l2_group * k_out_group;
	global_sz.y = c_in_lds_group;
	global_sz.z = 1;
	group_sz.y = c_in_lds_group;
	group_sz.z = 1;
	group_num = global_sz / group_sz;
	group_wave_num_x = group_sz.x / WAVE_SIZE;

	if (group_sz.x * group_sz.y > 1024)
		return E_ReturnState::RTN_ERR;

	print_kernel_param();

	return E_ReturnState::SUCCESS;
}

/************************************************************************************/
/* 卷积主体																			*/
/************************************************************************************/
void KernelWriterConv1x1::main_conv()
{
	// -------------------------------------------------------------------------------
	// 数据存储区域声明:
	// -------------------------------------------------------------------------------
	v_in_buff_a = newVgpr("v_in_a", c_in_maps_once_real);
	v_in_buff_b = newVgpr("v_in_b", c_in_maps_once_real);
	s_wei_buff_a = newSgpr("s_wei_a", c_in_maps_once_real, c_in_maps_once_real);
	s_wei_buff_b = newSgpr("s_wei_b", c_in_maps_once_real, c_in_maps_once_real);
	v_acc_buff = newVgpr("v_accum", k_out_maps, k_out_maps);
	s_prefetch = newSgpr("s_prefetch", k_out_maps);

	// -------------------------------------------------------------------------------
	// 卷积计算:
	// -------------------------------------------------------------------------------
	// 初始化:
	init_output();

	// 循环填充
	wei_offset = 0;
	prefetch_weight();
	load_input(v_in_buff_a);

	// 循环体
	if (conv_loop > 1)
	{
		Var * s_loop_cnt = newSgpr("s_loop_cnt");
		f_s_loop(s_loop_cnt, conv_loop - 1, "CONV_LOOP");
		{
			load_input(v_in_buff_b);
			if ((k_out_maps % 2) == 0)	conv_one_loop_even(v_in_buff_a, false);
			else						conv_one_loop_odd(v_in_buff_a, false);
			load_input(v_in_buff_a);
			if ((k_out_maps % 2) == 0)	conv_one_loop_even(v_in_buff_b, true);
			else						conv_one_loop_odd(v_in_buff_b, true);
		}
		f_e_loop(s_loop_cnt, "CONV_LOOP");
	}

	// 循环排空
	if (conv_loop > 0)
	{
		load_input(v_in_buff_b);
		if ((k_out_maps % 2) == 0)		conv_one_loop_even(v_in_buff_a, false);
		else							conv_one_loop_odd(v_in_buff_a, false);
		if ((k_out_maps % 2) == 0)		conv_last_loop_even(v_in_buff_b);
		else							conv_last_loop_odd(v_in_buff_b);
	}
	else
	{
		wei_offset -= (c_in_maps_once_real - wei_chan_stride * k_out_maps) * 4;
		if ((k_out_maps % 2) == 0)		conv_last_loop_even(v_in_buff_a);
		else							conv_last_loop_odd(v_in_buff_a);
	}

	// -------------------------------------------------------------------------------
	// 存储结果:
	// -------------------------------------------------------------------------------
	save_result();

	// -------------------------------------------------------------------------------
	// 销毁变量
	// -------------------------------------------------------------------------------
	// 销毁地址
	if (en_input_offset == true)	delVar(v_global_offset);
	else							delVar(v_addr_in);
	delVar(s_addr_wei);
	delVar(v_addr_out);
	if (EnBias == true)				delVar(v_addr_bias);
	if (Relu == PRELU)				delVar(s_slop);
	if (en_l2_sync == true)			delVar(s_addr_sig);
	if (c_in_lds_group > 1)			delVar(v_addr_lds);
	if (c_in_l2_split_group > 1)	delVar(v_addr_l2);

	// 销毁结果buffer
	delVar(v_acc_buff);
}
void KernelWriterConv1x1::conv_one_loop_even(Var * in_buff, bool is_pang_buff)
{
	// 调整weight buff偏移量
	if (is_pang_buff == true)
	{
		wei_offset += (c_in_maps_once_real - wei_chan_stride * k_out_maps) * 4;
	}
	else
	{
		wei_offset = 0;
	}

	load_weight(s_wei_buff_a);
	s_wait_lgkmcnt(0);				// wait s_wei_buff_a
	load_weight(s_wei_buff_b);
	s_wait_vmcnt(c_in_maps_once_real);				// wait in_buff
	conv_one_accum(in_buff, s_wei_buff_a, *v_acc_buff + 0);

	int loop = k_out_maps / 2 - 1;
	for (int i = 0; i < loop; i++)
	{
		s_wait_lgkmcnt(0);			// wait s_wei_buff_b
		load_weight(s_wei_buff_a);
		conv_one_accum(in_buff, s_wei_buff_b, *v_acc_buff + (i * 2 + 1));
		s_wait_lgkmcnt(0);			// wait s_wei_buff_a
		load_weight(s_wei_buff_b);
		conv_one_accum(in_buff, s_wei_buff_a, *v_acc_buff + (i * 2 + 2));
	}

	// 调整weight地址
	if (is_pang_buff == true)
	{
		op3("s_add_u32", s_addr_wei, s_addr_wei, c_in_maps_once_real * 2 * 4);
		op3("s_addc_u32", *s_addr_wei + 1, *s_addr_wei + 1, 0);
	}

	s_wait_lgkmcnt(0);			// wait s_wei_buff_b
	// 为下一轮循环预读取
	if (is_pang_buff == true)
	{
		prefetch_weight();
	}
	conv_one_accum(in_buff, s_wei_buff_b, *v_acc_buff + (k_out_maps - 1));
}
void KernelWriterConv1x1::conv_one_loop_odd(Var * in_buff, bool is_pang_buff)
{
	// 调整weight buff偏移量
	if (is_pang_buff == true)		wei_offset += (c_in_maps_once_real - wei_chan_stride * k_out_maps) * 4;
	else							wei_offset = 0;

	load_weight(s_wei_buff_a);
	s_wait_lgkmcnt(0);					// wait s_wei_buff_a
	if (k_out_maps > 1)
	{
		load_weight(s_wei_buff_b);
	}
	s_wait_vmcnt(c_in_maps_once_real);	// wait in_buff
	conv_one_accum(in_buff, s_wei_buff_a, *v_acc_buff + 0);

	int loop = (k_out_maps - 1) / 2;
	for (int i = 0; i < loop; i++)
	{
		s_wait_lgkmcnt(0);				// wait s_wei_buff_b
		load_weight(s_wei_buff_a);
		conv_one_accum(in_buff, s_wei_buff_b, *v_acc_buff + (i * 2 + 1));
		if (i < loop - 1)
		{
			s_wait_lgkmcnt(0);			// wait s_wei_buff_a
			load_weight(s_wei_buff_b);
			conv_one_accum(in_buff, s_wei_buff_a, *v_acc_buff + (i * 2 + 2));
		}
		else
		{
			// 调整weight地址
			if (is_pang_buff == true)
			{
				op3("s_add_u32", s_addr_wei, s_addr_wei, c_in_maps_once_real * 2 * 4);
				op3("s_addc_u32", *s_addr_wei + 1, *s_addr_wei + 1, 0);
			}
			s_wait_lgkmcnt(0);			// wait s_wei_buff_a
			// 为下一轮循环预读取
			if (is_pang_buff == true)
			{
				prefetch_weight();
			}
			conv_one_accum(in_buff, s_wei_buff_a, *v_acc_buff + (i * 2 + 2));
		}
	}
}
void KernelWriterConv1x1::conv_last_loop_even(Var * in_buff)
{
	// 调整weight buff偏移量
	wei_offset += (c_in_maps_once_real - wei_chan_stride * k_out_maps) * 4;

	load_weight(s_wei_buff_a);
	s_wait_lgkmcnt(0);				// wait s_wei_buff_a
	load_weight(s_wei_buff_b);
	s_wait_vmcnt(0);				// wait in_buff
	conv_one_accum(in_buff, s_wei_buff_a, *v_acc_buff + 0);

	int loop = k_out_maps / 2 - 1;
	for (int i = 0; i < loop; i++)
	{
		s_wait_lgkmcnt(0);			// wait s_wei_buff_b
		load_weight(s_wei_buff_a);
		conv_one_accum(in_buff, s_wei_buff_b, *v_acc_buff + (i * 2 + 1));
		s_wait_lgkmcnt(0);			// wait s_wei_buff_a
		load_weight(s_wei_buff_b);
		conv_one_accum(in_buff, s_wei_buff_a, *v_acc_buff + (i * 2 + 2));
	}

	s_wait_lgkmcnt(0);				// wait s_wei_buff_b
	conv_one_accum(in_buff, s_wei_buff_b, *v_acc_buff + (k_out_maps - 1));
}
void KernelWriterConv1x1::conv_last_loop_odd(Var * in_buff)
{
	// 调整weight buff偏移量
	wei_offset += (c_in_maps_once_real - wei_chan_stride * k_out_maps) * 4;

	load_weight(s_wei_buff_b);
	s_wait_vmcnt(0);				// wait in_buff

	int loop = (k_out_maps - 1) / 2;
	for (int i = 0; i < loop; i++)
	{
		s_wait_lgkmcnt(0);			// wait s_wei_buff_b
		load_weight(s_wei_buff_a);
		conv_one_accum(in_buff, s_wei_buff_b, *v_acc_buff + (i * 2));
		s_wait_lgkmcnt(0);			// wait s_wei_buff_a
		load_weight(s_wei_buff_b);
		conv_one_accum(in_buff, s_wei_buff_a, *v_acc_buff + (i * 2 + 1));
	}

	s_wait_lgkmcnt(0);				// wait s_wei_buff_b
	conv_one_accum(in_buff, s_wei_buff_b, *v_acc_buff + (k_out_maps - 1));
}
void KernelWriterConv1x1::conv_one_accum(Var * in_buff, Var * wei_buff, Var * accum)
{
	for (int i = 0; i < c_in_maps_once_real; i++)
	{
		// debug
		//op2("v_mov_b32", *in_buff + i, 1.000000001);
		//op2("s_mov_b32", *wei_buff + i, 1.0000000001);

		op3("v_mac_f32", accum, *in_buff + i, *wei_buff + i);
	}
}

/************************************************************************************/
/* 计算下标																			*/
/************************************************************************************/
E_ReturnState KernelWriterConv1x1::calcuIndex()
{
	// -------------------------------------------------------------------------------
	// kenrel 参数
	// -------------------------------------------------------------------------------
	{
		s_ptr_in = newSgpr("s_ptr_in", 2, 2);
		s_ptr_wei = newSgpr("s_ptr_wei", 2, 2);
		s_ptr_out = newSgpr("s_ptr_out", 2, 2);
		if (EnBias == true)				s_ptr_bias = newSgpr("s_ptr_bias", 2, 2);
		if (en_l2_sync)					s_ptr_sig = newSgpr("s_ptr_sig", 2, 2);
		if (c_in_l2_split_group > 1)	s_ptr_l2 = newSgpr("s_ptr_l2", 2, 2);
		if (Relu == PRELU)				s_slop = newSgpr("s_slop");
#if KERNEL_DEBUG
		s_ptr_dbg = newSgpr("s_ptr_dbg", 2, 2);
#endif
	}

	// -------------------------------------------------------------------------------
	// 中间变量
	// -------------------------------------------------------------------------------
	{
		v_wave_id_x = newVgpr("v_wave_id_x");
		v_wave_tid_x = newVgpr("v_wave_tid_x");
		v_pix_blk_id = newVgpr("v_pix_blk_id");
		v_c_blk_id = newVgpr("v_c_blk_id");
		v_c_l2_blk_id = newVgpr("v_c_l2_blk_id");
		v_k_blk_id = newVgpr("v_k_blk_id");
		v_pos_id = newVgpr("v_pos_id");
		v_batch_id = newVgpr("v_batch_id");
		v_out_id = newVgpr("v_out_id");
		v_l2_pos_id = newVgpr("v_l2_pos_id");
		v_lds_pos_id = newVgpr("v_lds_id");
		s_c_blk_id = newSgpr("s_c_blk_id");
		s_c_l2_blk_id = newSgpr("s_c_blk_id");
		s_c_lds_blk_id = newSgpr("s_c_blk_id");
	}

	// -------------------------------------------------------------------------------
	// 实际地址
	// -------------------------------------------------------------------------------
	{
		if (en_input_offset == true)	v_global_offset = newVgpr("v_global_offset", c_in_maps_once_real * 2 / 2, 2);
		else							v_addr_in = newVgpr("v_addr_in", 2, 2);
		s_addr_wei = newSgpr("s_addr_wei", 2, 2);
		v_addr_out = newVgpr("v_addr_out", 2, 2);
		if (EnBias == true)				v_addr_bias = newVgpr("v_addr_bias", 2, 2);
		if (en_l2_sync)					s_addr_sig = newSgpr("s_addr_sig", 2, 2);
		if (c_in_lds_group > 1)			v_addr_lds = newVgpr("v_ds_addr");
		if (c_in_l2_split_group > 1)	v_addr_l2 = newVgpr("v_addr_l2", 2, 2);
#if KERNEL_DEBUG
		v_addr_dbg = newVgpr("v_addr_dbg", 2, 2);
#endif
	}

	// -------------------------------------------------------------------------------
	// 计算过程
	// -------------------------------------------------------------------------------
	{
		ChkErr(loadKernelArgs());
		ChkErr(calcuWaveIndex());
		ChkErr(calcuBlkIndex());
		ChkErr(calcuPosIndex());
		ChkErr(calcuOffset());
	}

	// -------------------------------------------------------------------------------
	// 销毁 kenrel 参数
	// -------------------------------------------------------------------------------
	{
		if (en_input_offset == false)	delVar(s_ptr_in);
		delVar(s_ptr_wei);
		delVar(s_ptr_out);
		if (EnBias == true)				delVar(s_ptr_bias);
		if (en_l2_sync)					delVar(s_ptr_sig);
		if (c_in_l2_split_group > 1)	delVar(s_ptr_l2);
#if KERNEL_DEBUG
		delVar(s_ptr_dbg);
#endif
	}

	// -------------------------------------------------------------------------------
	// 销毁中间变量
	// -------------------------------------------------------------------------------
	{
		delVar(v_wave_id_x);
		delVar(v_wave_tid_x);
		delVar(v_pix_blk_id);
		delVar(v_c_blk_id);
		delVar(v_c_l2_blk_id);
		delVar(v_k_blk_id);
		delVar(v_pos_id);
		delVar(v_batch_id);
		delVar(v_out_id);
		delVar(v_lds_pos_id);
		delVar(v_l2_pos_id);
	}

	return E_ReturnState::SUCCESS;
}
E_ReturnState KernelWriterConv1x1::loadKernelArgs()
{
	int offset = 0;

	s_load_dword(2, s_ptr_in, s_kernelArg, offset);			offset += 8;
	s_load_dword(2, s_ptr_wei, s_kernelArg, offset);		offset += 8;
	s_load_dword(2, s_ptr_out, s_kernelArg, offset);		offset += 8;

	if (EnBias == true)
		s_load_dword(2, s_ptr_bias, s_kernelArg, offset);
	offset += 8;

	if (en_l2_sync == true)
		s_load_dword(2, s_ptr_sig, s_kernelArg, offset);
	offset += 8;

	if (c_in_l2_split_group > 1)
		s_load_dword(2, s_ptr_l2, s_kernelArg, offset);
	offset += 8;

	if (Relu == PRELU)
		s_load_dword(1, s_slop, s_kernelArg, offset);
	offset += 8;

#if KERNEL_DEBUG
	s_load_dword(2, s_ptr_dbg, s_kernelArg, offset);		offset += 8;
#endif

	return E_ReturnState::SUCCESS;
}
E_ReturnState KernelWriterConv1x1::calcuWaveIndex()
{
	Var * s_tmp1 = newSgpr("s_tmp1");
	Var * v_tmp1 = newVgpr("v_tmp1");

	// -------------------------------------------------------------------------------
	// wave_id_x = (group_wave_num_x * gid_x) + (tid_x / WAVE_SIZE);
	// wave_tid_x = tid_x % WAVE_SIZE;
	// -------------------------------------------------------------------------------
	op3("s_lshl_b32", s_tmp1, s_gid_x, log2(group_wave_num_x));		// s_tmp1 = gid_x * block_per_group
	op3("v_lshrrev_b32", v_tmp1, log2(WAVE_SIZE), v_tid_x);			// v_tmp1 = tid_x / WAVE_SIZE
	v_add_u32(v_wave_id_x, v_tmp1, s_tmp1);					// v_wave_id_x = s_tmp1 + v_tmp1
	op3("v_and_b32", v_wave_tid_x, modMask(WAVE_SIZE), v_tid_x);	// v_wave_tid_x = tid_x % WAVE_SIZE

	delVar(s_tmp1);
	delVar(v_tmp1);

	return E_ReturnState::SUCCESS;
}
E_ReturnState KernelWriterConv1x1::calcuBlkIndex()
{
	Var * v_tmp1 = newVgpr("v_tmp1");
	Var * v_tmp2 = newVgpr("v_tmp2");

	if (PCK_order == 321)
	{
		// -------------------------------------------------------------------------------
		// k_out --> c_in --> pix
		// k_blk_id = wave_id_x % k_out_group;
		// c_l2_blk_id = wave_id_x / k_out_group % c_in_l2_group;
		// pix_blk_id = wave_id_x / k_out_group / c_in_l2_group;
		// -------------------------------------------------------------------------------
		fv_div_u32(v_wave_id_x, k_out_group, v_tmp2, v_k_blk_id);
		fv_div_u32(v_tmp2, c_in_l2_group, v_pix_blk_id, v_c_l2_blk_id);
	}
	else if (PCK_order == 312)
	{
		// -------------------------------------------------------------------------------
		// c_in --> k_out --> pix
		// c_l2_blk_id = wave_id_x % c_in_l2_group;
		// k_blk_id = wave_id_x / c_in_l2_group % k_out_group;
		// pix_blk_id = wave_id_x / c_in_l2_group / k_out_group;
		// -------------------------------------------------------------------------------
		fv_div_u32(v_wave_id_x, c_in_l2_group, v_tmp2, v_c_l2_blk_id);
		fv_div_u32(v_tmp2, k_out_group, v_pix_blk_id, v_k_blk_id);
	}
	else if (PCK_order == 123)
	{
		// -------------------------------------------------------------------------------
		// pix --> c_in --> k_out
		// pix_blk_id = wave_id_x % pix_wave;
		// c_l2_blk_id = wave_id_x / pix_wave % c_in_l2_group;
		// k_blk_id = wave_id_x / pix_wave / c_in_l2_group;
		// -------------------------------------------------------------------------------
		fv_div_u32(v_wave_id_x, pix_wave, v_tmp2, v_pix_blk_id);
		fv_div_u32(v_tmp2, c_in_l2_group, v_k_blk_id, v_c_l2_blk_id);
	}
	else if (PCK_order == 132)
	{
		// -------------------------------------------------------------------------------
		// pix --> k_out --> c_in
		// pix_blk_id = wave_id_x % pix_wave;
		// k_blk_id = wave_id_x / pix_wave % k_out_group;
		// c_l2_blk_id = wave_id_x / pix_wave / k_out_group;
		// -------------------------------------------------------------------------------
		fv_div_u32(v_wave_id_x, pix_wave, v_tmp2, v_pix_blk_id);
		fv_div_u32(v_tmp2, k_out_group, v_c_l2_blk_id, v_k_blk_id);
	}
	else if (PCK_order == 213)
	{
		// -------------------------------------------------------------------------------
		// c_in --> pix --> k_out
		// c_l2_blk_id = wave_id_x % c_in_l2_group;
		// pix_blk_id = wave_id_x / c_in_l2_group % pix_wave;
		// k_blk_id = wave_id_x / c_in_l2_group / pix_wave;
		// -------------------------------------------------------------------------------
		fv_div_u32(v_wave_id_x, c_in_l2_group, v_tmp2, v_c_l2_blk_id);
		fv_div_u32(v_tmp2, pix_wave, v_k_blk_id, v_pix_blk_id);
	}
	else if (PCK_order == 231)
	{
		// -------------------------------------------------------------------------------
		// k_out --> pix --> c_in
		// k_blk_id = wave_id_x % k_out_group;
		// pix_blk_id = wave_id_x / k_out_group % pix_wave;
		// c_l2_blk_id = wave_id_x / k_out_group / pix_wave;
		// -------------------------------------------------------------------------------
		fv_div_u32(v_wave_id_x, k_out_group, v_tmp2, v_k_blk_id);
		fv_div_u32(v_tmp2, pix_wave, v_c_l2_blk_id, v_pix_blk_id);
	}

	// -------------------------------------------------------------------------------
	// c_blk_id = c_in_lds_group * c_l2_blk_id + tid_y;
	// -------------------------------------------------------------------------------
	op3("v_mul_u32_u24", v_tmp1, c_in_lds_group, v_c_l2_blk_id);
	v_add_u32(v_c_blk_id, v_tmp1, v_tid_y);

	op2("v_readfirstlane_b32", s_c_blk_id, v_c_blk_id);
	op2("v_readfirstlane_b32", s_c_l2_blk_id, v_c_l2_blk_id);
	op2("v_readfirstlane_b32", s_c_lds_blk_id, v_tid_y);

	delVar(v_tmp1);
	delVar(v_tmp2);

	return E_ReturnState::SUCCESS;
}
E_ReturnState KernelWriterConv1x1::calcuPosIndex()
{
	Var * v_tmp1 = newVgpr("v_tmp1");

	// -------------------------------------------------------------------------------
	// pos_id   = (pix_blk_id * WAVE_SIZE + wave_tid_x) % in_chan_stride;
	// batch_id = (pix_blk_id * WAVE_SIZE + wave_tid_x) / in_chan_stride;
	// out_id   = k_blk_id * k_out_maps;
	// -------------------------------------------------------------------------------
	op3("v_lshlrev_b32", v_tmp1, log2(WAVE_SIZE), v_pix_blk_id);
	v_add_u32(v_tmp1, v_wave_tid_x, v_tmp1);					// v_tmp1 = pix_blk_id * WAVE_SIZE + wave_tid_x
	fv_div_u32(v_tmp1, in_chan_stride, v_batch_id, v_pos_id);
	op3("v_mul_u32_u24", v_out_id, k_out_maps, v_k_blk_id);

	// -------------------------------------------------------------------------------
	// if (batch_id >= extProbCfg->N)
	//		return;
	// -------------------------------------------------------------------------------
	op2("v_mov_b32", v_tmp1, N);
	op3("v_cmpx_lt_u32", "vcc", v_batch_id, v_tmp1);
	op1("s_cbranch_execz", l_end_prg);

	// -------------------------------------------------------------------------------
	// lds_space_size = group_sz.x * k_out_maps * c_in_lds_split_group;
	// lds_pos_id = tid_y % c_in_lds_split_group;
	// -------------------------------------------------------------------------------
	ldsByteCount += group_sz.x * k_out_maps * c_in_lds_split_group * 4;
	fv_div_u32(v_tid_y, c_in_lds_split_group, v_tmp1, v_lds_pos_id);

	// -------------------------------------------------------------------------------
	// l2_pos_id = c_l2_blk_id % c_in_l2_split_group;
	// -------------------------------------------------------------------------------
	fv_div_u32(v_c_l2_blk_id, c_in_l2_split_group, v_tmp1, v_l2_pos_id);

	delVar(v_tmp1);

	return E_ReturnState::SUCCESS;
}
E_ReturnState KernelWriterConv1x1::calcuOffset()
{
	Var * s_tmp1 = newSgpr("s_tmp1");
	Var * v_tmp1 = newVgpr("v_tmp1");
	Var * v_tmp2 = newVgpr("v_tmp2");
	Var * v_tmp3 = newVgpr("v_tmp3");
	Var * v_out_off_tmp = newVgpr("v_out_off_tmp");

	s_wait_lgkmcnt(0);

	// -------------------------------------------------------------------------------
	// gbl_in_off = (batch_id * in_batch_stride) + (c_blk_id * c_in_maps * in_chan_stride) + pos_id;
	// -------------------------------------------------------------------------------
	{
		op2("v_mov_b32", v_tmp1, in_batch_stride);
		op3("v_mul_lo_u32", v_tmp1, v_tmp1, v_batch_id);		// v_tmp1 = batch_id * in_batch_stride
		op3("v_mul_u32_u24", v_tmp2, c_in_maps, v_c_blk_id);
		op3("v_mul_u32_u24", v_tmp2, in_chan_stride, v_tmp2);	// v_tmp2 = c_blk_id * c_in_maps * in_chan_stride

		v_add3_u32(v_tmp3, v_tmp1, v_tmp2, v_pos_id);
		op3("v_lshlrev_b32", v_tmp3, 2, v_tmp3);				// v_tmp3 = gbl_in_off (BYTE)

		if (en_input_offset == true)
		{
			op2("v_mov_b32", v_global_offset, v_tmp3);
			op2("v_mov_b32", v_tmp1, in_chan_stride * 2 * 4);
			for (int i = 0; i < c_in_maps_once_real * 2 / 2; i++)
			{
				v_addc_u32(*v_global_offset + 2 * (i + 1), *v_global_offset + 2 * i, v_tmp1);
			}
		}
		else
		{
			op2("v_mov_b32", *v_addr_in + 1, *s_ptr_in + 1);
			v_addc_u32(v_addr_in, s_ptr_in, v_tmp3);
			v_addc_co_u32(*v_addr_in + 1, 0, *v_addr_in + 1);
		}
	}

	// -------------------------------------------------------------------------------
	// wei_off = (out_id * wei_chan_stride) + (c_blk_id * c_in_maps);
	// -------------------------------------------------------------------------------
	{
		op3("v_mul_u32_u24", v_tmp1, wei_chan_stride, v_out_id);	// v_tmp1 = out_id * wei_chan_stride
		op3("v_mul_u32_u24", v_tmp2, c_in_maps, v_c_blk_id);		// v_tmp2 = c_blk_id * c_in_maps
		v_add_u32(v_tmp1, v_tmp1, v_tmp2);
		op2("v_readfirstlane_b32", s_tmp1, v_tmp1);					// s_tmp1 = wei_off
		op3("s_lshl_b32", s_tmp1, s_tmp1, 2);						// s_tmp1 = wei_off (BYTE)
		op3("s_add_u32", s_addr_wei, s_ptr_wei, s_tmp1);
		op3("s_addc_u32", *s_addr_wei + 1, 0, *s_ptr_wei + 1);
	}

	// -------------------------------------------------------------------------------
	// gbl_out_off = (batch_id * out_batch_stride) + (out_id * out_chan_stride) + pos_id;
	// -------------------------------------------------------------------------------
	{
		op2("v_mov_b32", v_tmp1, out_batch_stride);
		op3("v_mul_lo_u32", v_tmp1, v_tmp1, v_batch_id);			// v_tmp1 = batch_id * out_batch_stride
		op3("v_mul_u32_u24", v_tmp2, out_chan_stride, v_out_id);	// v_tmp2 = out_id * out_chan_stride

		v_add3_u32(v_tmp3, v_tmp1, v_tmp2, v_pos_id);
		op2("v_mov_b32", v_out_off_tmp, v_tmp3);
		op3("v_lshlrev_b32", v_tmp3, 2, v_tmp3);					// v_tmp3 = gbl_out_off (BYTE)

		op2("v_mov_b32", *v_addr_out + 1, *s_ptr_out + 1);
		v_addc_u32(v_addr_out, s_ptr_out, v_tmp3);
		v_addc_co_u32(*v_addr_out + 1, 0, *v_addr_out + 1);
	}

	// -------------------------------------------------------------------------------
	// bias_off = out_id
	// -------------------------------------------------------------------------------
	if (EnBias == true)
	{
		op3("v_lshlrev_b32", v_tmp1, 2, v_out_id);
		op2("v_mov_b32", *v_addr_bias + 1, *s_ptr_bias + 1);
		v_addc_u32(v_addr_bias, s_ptr_bias, v_tmp1);
		v_addc_co_u32(*v_addr_bias + 1, 0, *v_addr_bias + 1);
	}

	// -------------------------------------------------------------------------------
	// sig_off = pixBlkId * k_out_group + kOutBlkId
	// -------------------------------------------------------------------------------
	if (en_l2_sync)
	{
		op3("v_mul_u32_u24", v_tmp1, k_out_group, v_pix_blk_id);			// v_tmp1 = pixBlkId * k_out_group
		v_addc_u32(v_tmp1, v_tmp1, v_k_blk_id);
		op2("v_readfirstlane_b32", s_tmp1, v_tmp1);							// s_tmp1 = sig_off

		op3("s_lshl_b32", s_tmp1, s_tmp1, 2);								// s_tmp1 = sig_off (BYTE)
		s_wait_lgkmcnt(0);
		op3("s_add_u32", s_addr_sig, s_ptr_sig, s_tmp1);
		op3("s_addc_u32", *s_addr_sig + 1, 0, *s_ptr_sig + 1);
	}

	// -------------------------------------------------------------------------------
	// lds_off = (group_sz.x * k_out_maps) * lds_pos_id + tid_x
	// -------------------------------------------------------------------------------
	if (c_in_lds_group > 1)
	{
		op3("v_mul_u32_u24", v_tmp1, group_sz.x * k_out_maps, v_lds_pos_id);		// v_tmp1 = (group_sz.x * k_out_maps) * lds_pos_id
		v_add_u32(v_tmp1, v_tmp1, v_tid_x);				// v_tmp1 = lds_off
		op3("v_lshlrev_b32", v_addr_lds, 2, v_tmp1);
	}

	// -------------------------------------------------------------------------------
	// l2_off = out_size * l2_pos_id + gbl_out_off
	// -------------------------------------------------------------------------------
	if (c_in_l2_split_group > 1)
	{
		op2("v_mov_b32", v_tmp1, out_size);
		op3("v_mul_lo_u32", v_tmp1, v_tmp1, v_l2_pos_id);		// v_tmp1 = out_size * l2_pos_id
		v_add_u32(v_tmp1, v_tmp1, v_out_off_tmp);				// v_tmp1 = l2_off
		op3("v_lshlrev_b32", v_tmp1, 2, v_tmp1);

		op2("v_mov_b32", *v_addr_l2 + 1, *s_ptr_l2 + 1);
		v_addc_u32(v_addr_l2, s_ptr_l2, v_tmp1);
		v_addc_co_u32(*v_addr_l2 + 1, 0, *v_addr_l2 + 1);
	}

	// -------------------------------------------------------------------------------
	// dbg_off = 线性地址
	// -------------------------------------------------------------------------------
#if KERNEL_DEBUG
	f_linear_addr_2d(s_ptr_dbg, v_addr_dbg);
#endif

	delVar(s_tmp1);
	delVar(v_tmp1);
	delVar(v_tmp2);
	delVar(v_tmp3);
	delVar(v_out_off_tmp);

	return E_ReturnState::SUCCESS;
}

/************************************************************************************/
/* 数据读取与存储																		*/
/************************************************************************************/
#define SIG_INIT_VAL		11
void KernelWriterConv1x1::init_output()
{
	Var * v_init = newVgpr("v_init");
	op2("v_mov_b32", v_init, 0);

	// -------------------------------------------------------------------------------
	// 初始化累加器
	// -------------------------------------------------------------------------------
	for (int i = 0; i < k_out_maps; i++)
		op2("v_mov_b32", *v_acc_buff + i, 0);

	// -------------------------------------------------------------------------------
	// 第一个 c_in_blk 取bias
	// -------------------------------------------------------------------------------
	Var * l_end_bias_init = newLaber("END_BIAS_INIT");
	if (EnBias == true)
	{
		op2("s_cmpk_eq_i32", s_c_blk_id, 0);
		op1("s_cbranch_scc0", l_end_bias_init);

		for (int i = 0; i < k_out_maps; i++)
		{
			flat_load_dword(1, *v_acc_buff + i, v_addr_bias, "off");
			v_addc_u32(v_addr_bias, 4, v_addr_bias);
			v_addc_co_u32(*v_addr_bias + 1, 0, *v_addr_bias + 1);
		}
		s_wait_vmcnt(0);
		wrLaber(l_end_bias_init);
	}

	// -------------------------------------------------------------------------------
	// 第一个 tid_y 做LDS ATOMIC 初始化
	// -------------------------------------------------------------------------------
	Var * l_end_lds_atomic_init = newLaber("END_LDS_ATOMIC_INIT");
	if (c_in_lds_atomic_group > 1)
	{
		op2("s_cmpk_lt_i32", s_c_blk_id, c_in_lds_split_group);
		op1("s_cbranch_scc0", l_end_lds_atomic_init);

		Var * v_lds_addr_tmp = newVgpr("v_lds_addr_tmp");
		op2("v_mov_b32", v_lds_addr_tmp, v_addr_lds);

		for (int i = 0; i < k_out_maps; i++)
		{
			if (group_sz.x * k_out_maps * 4 <= MAX_16BIT_UINT)
			{
				ds_write_dword(1, v_lds_addr_tmp, v_init, group_sz.x * i * 4);
			}
			else
			{
				ds_write_dword(1, v_lds_addr_tmp, v_init, 0);
				v_add_u32(v_lds_addr_tmp, group_sz.x * 4, v_lds_addr_tmp);
			}
		}
		delVar(v_lds_addr_tmp);
	}
	s_wait_lgkmcnt(0);
	wrLaber(l_end_lds_atomic_init);

	// -------------------------------------------------------------------------------
	// 每个L2块的前split_group个块儿, 做L2 ATOMIC 初始化
	// -------------------------------------------------------------------------------
	Var * v_atomic_addr;
	if (c_in_l2_split_group > 1)	v_atomic_addr = v_addr_l2;
	else							v_atomic_addr = v_addr_out;
	Var * l_end_l2_atomic_init = newLaber("END_L2_ATOMIC_INIT");
	if (c_in_l2_atomic_group > 1)
	{
		op2("s_cmpk_lt_i32", s_c_l2_blk_id, c_in_l2_split_group);
		op1("s_cbranch_scc0", l_end_l2_atomic_init);

		Var * v_init_addr_tmp = newVgpr("v_l2_addr_tmp", 2, 2);
		op2("v_mov_b32", v_init_addr_tmp, v_atomic_addr);
		op2("v_mov_b32", *v_init_addr_tmp + 1, *v_atomic_addr + 1);
		for (int i = 0; i < k_out_maps; i++)
		{
			flat_store_dword(1, v_init_addr_tmp, v_init, "off");
			v_addc_u32(v_init_addr_tmp, out_chan_stride * 4, v_init_addr_tmp);
			v_addc_co_u32(*v_init_addr_tmp + 1, 0, *v_init_addr_tmp + 1);
		}
		delVar(v_init_addr_tmp);
	}
	s_wait_lgkmcnt(0);
	wrLaber(l_end_l2_atomic_init);

	// -------------------------------------------------------------------------------
	// 初始化信号槽
	// -------------------------------------------------------------------------------
	Var * l_end_signal_init = newLaber("END_SIGNAL_INIT");
	if (en_l2_sync == true)
	{
		op2("s_cmpk_eq_i32", s_c_blk_id, 0);
		op1("s_cbranch_scc0", l_end_signal_init);

		Var * s_tmp = newSgpr("s_tmp");
		op2("s_mov_b32", s_tmp, SIG_INIT_VAL);
		s_store_dword(1, s_tmp, s_addr_sig, 0, true);
		delVar(s_tmp);
	}
	s_wait_lgkmcnt(0);
	wrLaber(l_end_signal_init);

	delVar(v_init);
}
void KernelWriterConv1x1::load_input(Var * in_buff)
{
	if (en_input_offset == true)
	{
		if (c_in_maps_once_real >= 2)
		{
			for (int i = 0; i < c_in_maps_once_real / 2; i++)
			{
				flat_load_dword(1, *(*in_buff + 2 * i) + 0, *v_global_offset + 2 * i, s_ptr_in);
				flat_load_dword(1, *(*in_buff + 2 * i) + 1, *v_global_offset + 2 * i, s_ptr_in, in_chan_stride * 4);
			}
		}
		else
		{
			flat_load_dword(1, in_buff, v_global_offset, s_ptr_in);
		}
		op3("s_add_u32", s_ptr_in, s_ptr_in, in_chan_stride * c_in_maps_once_real * 4);
		op3("s_addc_u32", *s_ptr_in + 1, 0, *s_ptr_in + 1);
	}
	else
	{
		if (c_in_maps_once_real >= 2)
		{
			for (int i = 0; i < c_in_maps_once_real; i++)
			{
				flat_load_dword(1, *in_buff + i, v_addr_in, "off");
				v_addc_u32(v_addr_in, in_chan_stride * 4, v_addr_in);
				v_addc_co_u32(*v_addr_in + 1, 0, *v_addr_in + 1);
			}
		}
		else
		{
			flat_load_dword(1, in_buff, v_addr_in, "off");
			v_addc_u32(v_addr_in, in_chan_stride * 4, v_addr_in);
			v_addc_co_u32(*v_addr_in + 1, 0, *v_addr_in + 1);
		}
	}
}
void KernelWriterConv1x1::load_weight(Var * wei_buff)
{
	s_load_dword(c_in_maps_once_real, wei_buff, s_addr_wei, wei_offset);
	if (k_out_maps > 1)
		wei_offset += wei_chan_stride * 4;
}
void KernelWriterConv1x1::prefetch_weight()
{
	if (en_wei_addr_offset == true)
	{
		for (int i = 0; i < k_out_maps; i++)
		{
			s_load_dword(1, *s_prefetch + i, s_addr_wei, wei_chan_stride * i * 4);
		}
	}
	else
	{
		Var * s_tmp = newSgpr("s_tmp", 2, 2);

		op2("s_mov_b32", s_tmp, s_addr_wei);
		op2("s_mov_b32", *s_tmp + 1, *s_addr_wei + 1);

		for (int i = 0; i < k_out_maps; i++)
		{
			s_load_dword(1, *s_prefetch + i, s_addr_wei, 0);

			op3("s_add_u32", s_addr_wei, s_addr_wei, wei_chan_stride * 4);
			op3("s_addc_u32", *s_addr_wei + 1, *s_addr_wei + 1, 0);
		}

		op2("s_mov_b32", s_addr_wei, s_tmp);
		op2("s_mov_b32", *s_addr_wei + 1, *s_tmp + 1);

		delVar(s_tmp);
	}
}

void KernelWriterConv1x1::save_result()
{
	// 销毁数据buffer
	delVar(v_in_buff_a);
	delVar(v_in_buff_b);
	delVar(s_wei_buff_a);
	delVar(s_wei_buff_b);
	delVar(s_prefetch);

	if (c_in_lds_group > 1)
	{
		if (c_in_lds_atomic_group > 1)		save_to_lds_atomic();
		if (c_in_lds_split_group > 1)		save_to_lds_split();
		if (c_in_l2_group < 2)				save_to_output();
	}

	if (c_in_l2_group > 1)
	{
		if (c_in_l2_atomic_group > 1)		save_to_l2_atomic();
		if (c_in_l2_split_group > 1)		save_to_l2_split();
		if (en_l2_sync == true)				save_to_output();
	}

	if ((c_in_lds_group < 2) && (c_in_l2_group < 2))
	{
		save_to_output();
	}
}
void KernelWriterConv1x1::save_to_output()
{
	Var * s_exec_bck = newSgpr("s_exec_bck", 2, 2);

	for (int i = 0; i < k_out_maps; i++)
	{
		// debug
		//op2("v_mov_b32", v_debug, s_c_l2_blk_id);
		//op2("v_cvt_f32_u32", v_debug, v_debug);
		//op2("v_mov_b32", *v_acc_buff + i, v_debug);

		if (Relu == RELU)
		{
			op2("s_mov_b64", *s_exec_bck ^ 2, "exec");
			op3("v_cmpx_lt_f32", "vcc", *v_acc_buff + i, 0);
			op2("v_mov_b32", *v_acc_buff + i, 0);
			op2("s_mov_b64", "exec", *s_exec_bck ^ 2);
		}
		else if (Relu == PRELU)
		{
			op2("s_mov_b64", *s_exec_bck ^ 2, "exec");
			op3("v_cmpx_lt_f32", "vcc", *v_acc_buff + i, 0);
			op3("v_mul_f32", *v_acc_buff + i, *v_acc_buff + i, s_slop);
			op2("s_mov_b64", "exec", *s_exec_bck ^ 2);
		}

		flat_store_dword(1, v_addr_out, *v_acc_buff + i, "off");
		v_addc_u32(v_addr_out, out_chan_stride * 4, v_addr_out);
		v_addc_co_u32(*v_addr_out + 1, 0, *v_addr_out + 1);
	}

	delVar(s_exec_bck);
}
void KernelWriterConv1x1::save_to_lds_atomic()
{
	Var * l_end_lds_atomic = newLaber("END_LDS_ATOMIC");

	// -------------------------------------------------------------------------------
	// 将当前wave的结果存到lds
	// -------------------------------------------------------------------------------
	Var * v_lds_addr_tmp = newVgpr("v_lds_addr_bck");
	op2("v_mov_b32", v_lds_addr_tmp, v_addr_lds);

	for (int i = 0; i < k_out_maps; i++)
	{
		// debug
		//op2("v_mov_b32", v_debug, v_tid_x);
		//op2("v_cvt_f32_u32", v_debug, v_debug);
		//op2("v_mov_b32", *v_acc_buff + i, v_debug);

		if (group_sz.x * k_out_maps * 4 <= MAX_16BIT_UINT) // 16-bit uint
		{
			op2("ds_add_f32", v_lds_addr_tmp, *v_acc_buff + i, group_sz.x * i * 4);
		}
		else
		{
			op2("ds_add_f32", v_lds_addr_tmp, *v_acc_buff + i, 0);
			v_add_u32(v_lds_addr_tmp, group_sz.x * 4, v_lds_addr_tmp);
		}
	}
	s_wait_lgkmcnt(0);
	delVar(v_lds_addr_tmp);

	// -------------------------------------------------------------------------------
	// 如果不进行split, 则需要同步后将数据从 LDS 读出到VGPR
	// -------------------------------------------------------------------------------
	if (c_in_lds_split_group < 2)
	{
		// -------------------------------------------------------------------------------
		// 同步
		// -------------------------------------------------------------------------------
		lds_wave_sync();

		// -------------------------------------------------------------------------------
		// 最后一个 tid_y 读取LDS到 v_acc_buff
		// -------------------------------------------------------------------------------
		op3("v_cmpx_eq_u32", "vcc", v_tid_y, group_sz.y - 1);
		op1("s_cbranch_execz", l_end_prg);

		for (int i = 0; i < k_out_maps; i++)
		{
			if (group_sz.x * k_out_maps * 4 <= MAX_16BIT_UINT) // 16-bit uint
			{
				ds_read_dword(1, *v_acc_buff + i, v_addr_lds, group_sz.x * i * 4);
			}
			else
			{
				ds_read_dword(1, *v_acc_buff + i, v_addr_lds, 0);
				v_add_u32(v_addr_lds, group_sz.x * 4, v_addr_lds);
			}
		}
		s_wait_lgkmcnt(0);
	}

	wrLaber(l_end_lds_atomic);
}
void KernelWriterConv1x1::save_to_lds_split()
{
	Var * l_end_lds_split = newLaber("END_LDS_SPLIT");
	Var * v_lds_addr_tmp = newVgpr("v_lds_addr_bck");
	Var * v_tmp1 = newVgpr("v_tmp1");

	// -------------------------------------------------------------------------------
	// 如果不进行atomic, 则需要进行存储 然后同步
	// -------------------------------------------------------------------------------
	if (c_in_lds_atomic_group < 2)
	{
		// 将当前wave的结果存到lds
		op2("v_mov_b32", v_lds_addr_tmp, v_addr_lds);
		for (int i = 0; i < k_out_maps; i++)
		{
			// debug
			//op2("v_mov_b32", v_debug, 1);
			//op2("v_cvt_f32_u32", v_debug, v_debug);
			//op2("v_mov_b32", *v_acc_buff + i, v_debug);

			if (group_sz.x * k_out_maps * 4 <= MAX_16BIT_UINT) // 16-bit uint
			{
				ds_write_dword(1, v_lds_addr_tmp, *v_acc_buff + i, group_sz.x * i * 4);
			}
			else
			{
				ds_write_dword(1, v_lds_addr_tmp, *v_acc_buff + i, 0);
				v_add_u32(v_lds_addr_tmp, group_sz.x * 4, v_lds_addr_tmp);
			}
		}
		s_wait_lgkmcnt(0);
	}

	// 同步
	lds_wave_sync();

	// -------------------------------------------------------------------------------
	// 最后一个 tid_y 读取LDS并累加到 v_acc_buff
	// -------------------------------------------------------------------------------
	op3("v_cmpx_eq_u32", "vcc", v_tid_y, group_sz.y - 1);
	op1("s_cbranch_execz", l_end_prg);

	Var * v_acc_buff1 = newVgpr("v_accum1", k_out_maps);
	Var * v_acc_buff2 = newVgpr("v_accum2", k_out_maps);
	Var * v_acc_buff0 = v_acc_buff1;

	// 第一轮
	{
		// 地址调整: v_addr_lds 指向第一块LDS
		op3("v_lshlrev_b32", v_addr_lds, 2, v_tid_x);
		// 读取第一组			
		for (int i = 0; i < k_out_maps; i++)
		{
			if (group_sz.x * k_out_maps * 4 <= MAX_16BIT_UINT) // 16-bit uint
			{
				ds_read_dword(1, *v_acc_buff + i, v_addr_lds, group_sz.x * i * 4);
			}
			else
			{
				ds_read_dword(1, *v_acc_buff + i, v_addr_lds, 0);
				v_add_u32(v_addr_lds, group_sz.x * 4, v_addr_lds);
			}
		}

		// 交换buffer
		v_acc_buff0 = v_acc_buff1;
	}
	// 循环
	for (int blk = 0; blk < c_in_lds_split_group - 2; blk++)
	{
		// 地址调整
		if (group_sz.x * k_out_maps * (c_in_lds_split_group - 1) * 4 <= MAX_16BIT_UINT)
		{
			v_add_u32(v_addr_lds, group_sz.x * k_out_maps * 4, v_addr_lds);
		}
		// 读取下一组
		for (int i = 0; i < k_out_maps; i++)
		{
			if (group_sz.x * k_out_maps * 4 <= MAX_16BIT_UINT) // 16-bit uint
			{
				ds_read_dword(1, *v_acc_buff0 + i, v_addr_lds, group_sz.x * i * 4);
			}
			else
			{
				ds_read_dword(1, *v_acc_buff0 + i, v_addr_lds, 0);
				v_add_u32(v_addr_lds, group_sz.x * 4, v_addr_lds);
			}
		}
		// 交换buffer
		if (v_acc_buff0 == v_acc_buff1)		v_acc_buff0 = v_acc_buff2;
		else								v_acc_buff0 = v_acc_buff1;

		// 累加上一组
		if (blk > 0)
		{
			if (k_out_maps > 15)	s_wait_lgkmcnt(15);
			else					s_wait_lgkmcnt(k_out_maps);

			for (int i = 0; i < k_out_maps; i++)
			{
				op3("v_add_f32", *v_acc_buff + i, *v_acc_buff + i, *v_acc_buff0 + i);
			}
		}
	}
	// 最后一轮
	{
		// 地址调整
		if (group_sz.x * k_out_maps * (c_in_lds_split_group - 1) * 4 <= MAX_16BIT_UINT)
		{
			v_add_u32(v_addr_lds, group_sz.x * k_out_maps * 4, v_addr_lds);
		}
		// 读取最后一组
		for (int i = 0; i < k_out_maps; i++)
		{
			if (group_sz.x * k_out_maps * 4 <= MAX_16BIT_UINT) // 16-bit uint
			{
				ds_read_dword(1, *v_acc_buff0 + i, v_addr_lds, group_sz.x * i * 4);
			}
			else
			{
				ds_read_dword(1, *v_acc_buff0 + i, v_addr_lds, 0);
				v_add_u32(v_addr_lds, group_sz.x * 4, v_addr_lds);
			}
		}
		// 累加
		s_wait_lgkmcnt(0);
		for (int i = 0; i < k_out_maps; i++)
		{
			if (c_in_lds_split_group == 2)
			{
				op3("v_add_f32", *v_acc_buff + i, *v_acc_buff + i, *v_acc_buff1 + i);
			}
			else
			{
				op3("v_add_f32", *v_acc_buff + i, *v_acc_buff + i, *v_acc_buff1 + i);
				op3("v_add_f32", *v_acc_buff + i, *v_acc_buff + i, *v_acc_buff2 + i);
			}
		}
	}

	wrLaber(l_end_lds_split);

	delVar(v_tmp1);
	delVar(v_acc_buff1);
	delVar(v_acc_buff2);
	delVar(v_lds_addr_tmp);
}
void KernelWriterConv1x1::save_to_l2_atomic()
{
	// v_newVal = v_src_cmp
	// v_prevVal = v_src_cmp + 1
	Var * v_src_cmp = newVgpr("v_src_cmp", 2, 2);
	Var * v_rtn = newVgpr("v_rtn");
	Var * v_tmp = newVgpr("v_tmp");
	Var * s_exec2 = newSgpr("s_exec2", 2, 2);
	Var * s_exec_bck = newSgpr("s_exec_bck", 2, 2);
	Var * v_addr_tmp = newVgpr("v_addr_tmp", 2, 2);
	Var * v_atomic_addr;

	// -------------------------------------------------------------------------------
	// 确定地址
	// -------------------------------------------------------------------------------
	if (c_in_l2_split_group > 1)	v_atomic_addr = v_addr_l2;
	else							v_atomic_addr = v_addr_out;

	// -------------------------------------------------------------------------------
	// 将当前wave的结果存到l2
	// -------------------------------------------------------------------------------
	Var * l_l2_atomic;
	op2("s_mov_b64", *s_exec_bck ^ 2, "exec");
	op2("v_mov_b32", v_addr_tmp, v_atomic_addr);
	op2("v_mov_b32", *v_addr_tmp + 1, *v_atomic_addr + 1);
	for (int i = 0; i < k_out_maps; i++)
	{
		// debug
		//op2("v_mov_b32", v_debug, 1);
		//op2("v_cvt_f32_u32", v_debug, v_debug);
		//op2("v_mov_b32", *v_acc_buff + i, v_debug);

		flat_load_dword(1, *v_src_cmp + 1, v_addr_tmp, "off", 0, true);
		s_wait_vmcnt(0);
		l_l2_atomic = newLaber("L2_ATOMIC_" + d2s(i));
		wrLaber(l_l2_atomic);
		{
			if (Relu == PRELU)
			{
				/* -------------------------------------------------------------------------------
				if (old < 0)
				{
					tmp = 1 / slop;
					old = old * tmp;
				}
				xin = old + acc;
				if (xin < 0)
					xin *= slop;
				------------------------------------------------------------------------------- */
				op2("s_mov_b64", *s_exec2 ^ 2, "exec");
				op2("v_mov_b32", v_tmp, *v_src_cmp + 1);
				op3("v_cmpx_lt_f32", "vcc", *v_src_cmp + 1, 0);
				op2("v_rcp_f32", v_tmp, s_slop);				// v_tmp = 1 / s_slop
				op3("v_mul_f32", v_tmp, *v_src_cmp + 1, v_tmp);	// v_tmp = old / s_slop
				op2("s_mov_b64", "exec", *s_exec2 ^ 2);
				op3("v_add_f32", v_src_cmp, v_tmp, *v_acc_buff + i);
				op3("v_cmpx_lt_f32", "vcc", v_src_cmp, 0);
				op3("v_mul_f32", v_src_cmp, v_src_cmp, s_slop);
				op2("s_mov_b64", "exec", *s_exec2 ^ 2);
			}
			else
			{
				op3("v_add_f32", v_src_cmp, *v_src_cmp + 1, *v_acc_buff + i);
			}

			if (IsaArch == E_IsaArch::Gfx800)
			{
				op3("flat_atomic_cmpswap", v_rtn, *v_addr_tmp ^ 2, *v_src_cmp ^ 2, true);
			}
			else if (IsaArch == E_IsaArch::Gfx900)
			{
				op4("global_atomic_cmpswap", v_rtn, *v_addr_tmp ^ 2, *v_src_cmp ^ 2, "off", true);
			}
			s_wait_vmcnt(0);
			op3("v_cmpx_neq_f32", "vcc", *v_src_cmp + 1, v_rtn);
			op2("v_mov_b32", *v_src_cmp + 1, v_rtn);
			op1("s_cbranch_execnz", l_l2_atomic);
		}

		op0("s_barrier");
		op2("s_mov_b64", "exec", *s_exec_bck ^ 2);
		v_addc_u32(v_addr_tmp, out_chan_stride * 4, v_addr_tmp);
		v_addc_co_u32(*v_addr_tmp + 1, 0, *v_addr_tmp + 1);
	}

	// -------------------------------------------------------------------------------
	// 只有当 Relu 且不进行 spilt 时才需要读出L2数据到寄存器, 因为l2_split()函数也会读出来
	// -------------------------------------------------------------------------------
	op2("v_mov_b32", v_addr_tmp, v_atomic_addr);
	op2("v_mov_b32", *v_addr_tmp + 1, *v_atomic_addr + 1);
	if ((Relu == RELU) && (c_in_l2_split_group < 2))
	{
		// -------------------------------------------------------------------------------
		// 同步
		// -------------------------------------------------------------------------------
		l2_wave_sync();

		// -------------------------------------------------------------------------------
		// 最后一个 c_blk_id 读取L2到 v_acc_buff
		// -------------------------------------------------------------------------------
		op2("s_cmpk_eq_i32", s_c_l2_blk_id, c_in_l2_group - 1);
		op1("s_cbranch_scc0", l_end_prg);
		for (int i = 0; i < k_out_maps; i++)
		{
			flat_load_dword(1, *v_acc_buff + i, v_addr_tmp, "off");
			v_addc_u32(v_addr_tmp, out_chan_stride * 4, v_addr_tmp);
			v_addc_co_u32(*v_addr_tmp + 1, 0, *v_addr_tmp + 1);
		}
		s_wait_vmcnt(0);
	}

	delVar(v_src_cmp);
	delVar(v_rtn);
	delVar(v_tmp);
	delVar(s_exec2);
	delVar(s_exec_bck);
	delVar(v_addr_tmp);
}
void KernelWriterConv1x1::save_to_l2_split()
{
	// -------------------------------------------------------------------------------
	// 如果不进行atomic, 则需要进行存储 然后同步
	// -------------------------------------------------------------------------------
	if (c_in_l2_atomic_group < 2)
	{
		for (int i = 0; i < k_out_maps; i++)
		{
			// debug
			//op2("v_mov_b32", v_debug, v_tid_x);
			//op2("v_cvt_f32_u32", v_debug, v_debug);
			//op2("v_mov_b32", *v_acc_buff + i, v_debug);

			flat_store_dword(1, v_addr_l2, *v_acc_buff + i, "off");
			v_addc_u32(v_addr_l2, out_chan_stride * 4, v_addr_l2);
			v_addc_co_u32(*v_addr_l2 + 1, 0, *v_addr_l2 + 1);
		}
	}

	// 同步
	l2_wave_sync();

	// -------------------------------------------------------------------------------
	// 最后一个 tid_y 读取LDS并累加到 v_acc_buff
	// -------------------------------------------------------------------------------
	op2("s_cmpk_eq_i32", s_c_l2_blk_id, c_in_l2_group - 1);
	op1("s_cbranch_scc0", l_end_prg);

	Var * v_acc_buff1 = newVgpr("v_accum1", k_out_maps);
	Var * v_acc_buff2 = newVgpr("v_accum2", k_out_maps);
	Var * v_acc_buff0 = v_acc_buff1;
	Var * v_tmp1 = newVgpr("v_tmp1");

	// 第一轮
	{
		// 地址调整: v_addr_l2 指向第一块L2
		if (c_in_l2_atomic_group < 2)
			op2("v_mov_b32", v_tmp1, (out_size * (c_in_l2_split_group - 1) + out_chan_stride * k_out_maps) * 4);
		else
			op2("v_mov_b32", v_tmp1, out_size * (c_in_l2_split_group - 1) * 4);
		v_subb_u32(v_addr_l2, v_addr_l2, v_tmp1);
		v_subb_co_u32(*v_addr_l2 + 1, *v_addr_l2 + 1, 0);
		// 读取第一组
		for (int i = 0; i < k_out_maps; i++)
		{
			flat_load_dword(1, *v_acc_buff + i, v_addr_l2, "off");
			v_addc_u32(v_addr_l2, out_chan_stride * 4, v_addr_l2);
			v_addc_co_u32(*v_addr_l2 + 1, 0, *v_addr_l2 + 1);
		}
		// 交换buffer
		v_acc_buff0 = v_acc_buff1;
	}

	// 循环
	for (int blk = 0; blk < c_in_l2_split_group - 2; blk++)
	{
		// 地址调整
		op2("v_mov_b32", v_tmp1, (out_size - out_chan_stride * k_out_maps) * 4);
		v_addc_u32(v_addr_l2, v_addr_l2, v_tmp1);
		v_addc_co_u32(*v_addr_l2 + 1, *v_addr_l2 + 1, 0);
		// 读取下一组
		for (int i = 0; i < k_out_maps; i++)
		{
			flat_load_dword(1, *v_acc_buff0 + i, v_addr_l2, "off");
			v_addc_u32(v_addr_l2, out_chan_stride * 4, v_addr_l2);
			v_addc_co_u32(*v_addr_l2 + 1, 0, *v_addr_l2 + 1);
		}
		// 交换buffer
		if (v_acc_buff0 == v_acc_buff1)	v_acc_buff0 = v_acc_buff2;
		else							v_acc_buff0 = v_acc_buff1;
		// 累加上一组
		if (blk > 0)
		{
			if (k_out_maps > 15)	s_wait_vmcnt(15);
			else				s_wait_vmcnt(k_out_maps);
			for (int i = 0; i < k_out_maps; i++)
			{
				op3("v_add_f32", *v_acc_buff + i, *v_acc_buff + i, *v_acc_buff0 + i);
			}
		}
	}

	// 最后一轮
	{
		// 地址调整
		op2("v_mov_b32", v_tmp1, (out_size - out_chan_stride * k_out_maps) * 4);
		v_addc_u32(v_addr_l2, v_addr_l2, v_tmp1);
		v_addc_co_u32(*v_addr_l2 + 1, *v_addr_l2 + 1, 0);
		// 读取最后一组
		for (int i = 0; i < k_out_maps; i++)
		{
			flat_load_dword(1, *v_acc_buff0 + i, v_addr_l2, "off");
			v_addc_u32(v_addr_l2, out_chan_stride * 4, v_addr_l2);
			v_addc_co_u32(*v_addr_l2 + 1, 0, *v_addr_l2 + 1);
		}
		// 累加
		s_wait_vmcnt(0);
		for (int i = 0; i < k_out_maps; i++)
		{
			if (c_in_l2_split_group == 2)
			{
				op3("v_add_f32", *v_acc_buff + i, *v_acc_buff + i, *v_acc_buff1 + i);
			}
			else
			{
				op3("v_add_f32", *v_acc_buff + i, *v_acc_buff + i, *v_acc_buff1 + i);
				op3("v_add_f32", *v_acc_buff + i, *v_acc_buff + i, *v_acc_buff2 + i);
			}
		}
	}

	delVar(v_tmp1);
	delVar(v_acc_buff1);
	delVar(v_acc_buff2);
}

/************************************************************************************/
/* wave 间同步																		*/
/************************************************************************************/
void KernelWriterConv1x1::lds_wave_sync()
{
	op0("s_barrier");
}
void KernelWriterConv1x1::l2_wave_sync()
{
	Var * s_sig = newSgpr("s_sig");
	Var * v_sig = newVgpr("v_sig");

	// -------------------------------------------------------------------------------
	// 检查初始化是否完成
	// -------------------------------------------------------------------------------
	Var * l_sgn_init_chk = newLaber("SGN_INIT_CHK");
	wrLaber(l_sgn_init_chk);
	{
		s_load_dword(1, s_sig, s_addr_sig, 0, true);
		s_wait_lgkmcnt(0);
		op2("s_cmpk_ge_i32", s_sig, SIG_INIT_VAL);
		op1("s_cbranch_scc0", l_sgn_init_chk);
		op2("s_cmpk_le_i32", s_sig, SIG_INIT_VAL + c_in_l2_group);
		op1("s_cbranch_scc0", l_sgn_init_chk);
	}

	// -------------------------------------------------------------------------------
	// 写信号
	// -------------------------------------------------------------------------------
	Var * s_exec_bck = newSgpr("exec_bck", 2, 2);
	Var * v_addr_sig = newVgpr("v_addr_sig", 2, 2);

	op2("s_mov_b64", *s_exec_bck ^ 2, "exec");
	op2("v_mov_b32", v_addr_sig, s_addr_sig);
	op2("v_mov_b32", *v_addr_sig + 1, *s_addr_sig + 1);
	op2("s_mov_b64", "exec", 1);
	op2("v_mov_b32", v_sig, 1);
	op3("flat_atomic_add", v_sig, *v_addr_sig ^ 2, v_sig);
	s_wait_vmcnt(0);
	op2("s_mov_b64", "exec", *s_exec_bck ^ 2);

	// -------------------------------------------------------------------------------
	// 最后一个 c_in_block 的 wave 读取信号
	// -------------------------------------------------------------------------------
	Var * l_l2_sync = newLaber("L2_WAVE_SYNC");
	Var * l_l2_sync_end = newLaber("END_L2_WAVE_SYNC");
	Var * s_timeout = newSgpr("s_timeout");
	op2("s_mov_b32", s_timeout, 0);
	op2("s_cmpk_eq_i32", s_c_l2_blk_id, c_in_l2_group - 1);
	op1("s_cbranch_scc0", l_end_prg);
#define TIMEOUT_LMT	1000
	wrLaber(l_l2_sync);
	{
		s_load_dword(1, s_sig, s_addr_sig, 0, true);
		// 超时检查
		op3("s_add_u32", s_timeout, s_timeout, 1);
		op2("s_cmp_eq_u32", s_timeout, TIMEOUT_LMT);
		op1("s_cbranch_scc1", l_l2_sync_end);
		s_wait_lgkmcnt(0);
		op2("s_cmpk_eq_i32", s_sig, SIG_INIT_VAL + c_in_l2_group);
		op1("s_cbranch_scc0", l_l2_sync);
	}
	wrLaber(l_l2_sync_end);
#undef TIMEOUT_LMT
	delVar(s_timeout);
}
#undef SIG_INIT_VAL

/************************************************************************************/
/* 测试																				*/
/************************************************************************************/
void KernelWriterConv1x1::print_kernel_param()
{
	INFO("");
	INFO("- Kernel Param:");
	INFO("- 	PCK_order = %d", PCK_order);
	INFO("- 	c_lds_atomic = %d, \tc_lds_split = %d", c_in_lds_atomic_group, c_in_lds_split_group);
	INFO("- 	c_l2_atomic = %d, \tc_l2_split = %d", c_in_l2_atomic_group, c_in_l2_split_group);
	if (c_in_maps < 10)
		INFO("- 	c_in_maps = %d, \t\tc_in_group = %d", c_in_maps, c_in_group);
	else
		INFO("- 	c_in_maps = %d, \tc_in_group = %d", c_in_maps, c_in_group);
	INFO("- 	k_out_maps = %d, \tk_out_group = %d", k_out_maps, k_out_group);
	INFO("- 	align = %d, \t\tpix_group = %d", align, pix_group);
	INFO("- 	group_size = %d, %d", group_sz.x, group_sz.y);
	INFO("- 	sigal_size = %d B, l2_size = %.2f MB, debug_size = %d", size_sig, size_l2 / 1024.0 / 1024.0, size_dbg);
	INFO("");
}
E_ReturnState KernelWriterConv1x1::simulate_index()
{
	uint tid_x = 1, tid_y = 0, gid_x;
	int wave_id_x = -1, wave_tid_x = -1;
	int pix_blk_id = -1, k_blk_id = -1, c_l2_blk_id = -1, c_blk_id = -1;
	int pos_id = -1, batch_id = -1, out_id = -1;
	int gbl_in_off = -1, wei_off = -1, gbl_out_off = -1;
	int lds_space_size = 0, l2_space_size = 0;
	int lds_split_id = -1, l2_split_id = -1;

	int *test_idx1 = (int*)malloc(group_num.x * sizeof(int));

	for (int grp = 0; grp < group_num.x; grp++)
	{
		gid_x = grp;

		// calcuWaveIndex()
		// wave_id = (group_wave_num_x * (group_sz.y * gid_x + tid_y)) + (tid_x / WAVE_SIZE);
		wave_id_x = (group_wave_num_x * gid_x) + (tid_x / WAVE_SIZE);
		wave_tid_x = tid_x % WAVE_SIZE;

		// calcuBlkIndex();
		if (PCK_order == 321)
		{
			// k_out --> c_in --> pix
			k_blk_id = wave_id_x % k_out_group;
			c_l2_blk_id = wave_id_x / k_out_group % c_in_l2_group;
			pix_blk_id = wave_id_x / k_out_group / c_in_l2_group;
		}
		else if (PCK_order == 312)
		{
			// c_in --> k_out --> pix
			c_l2_blk_id = wave_id_x % c_in_l2_group;
			k_blk_id = wave_id_x / c_in_l2_group % k_out_group;
			pix_blk_id = wave_id_x / c_in_l2_group / k_out_group;
		}
		else if (PCK_order == 123)
		{
			// pix --> c_in --> k_out
			pix_blk_id = wave_id_x % pix_wave;
			c_l2_blk_id = wave_id_x / pix_wave % c_in_l2_group;
			k_blk_id = wave_id_x / pix_wave / c_in_l2_group;
		}
		else if (PCK_order == 132)
		{
			// pix --> k_out --> c_in
			pix_blk_id = wave_id_x % pix_wave;
			k_blk_id = wave_id_x / pix_wave % k_out_group;
			c_l2_blk_id = wave_id_x / pix_wave / k_out_group;
		}
		else if (PCK_order == 213)
		{
			// c_in --> pix --> k_out
			c_l2_blk_id = wave_id_x % c_in_l2_group;
			pix_blk_id = wave_id_x / c_in_l2_group % pix_wave;
			k_blk_id = wave_id_x / c_in_l2_group / pix_wave;
		}
		else if (PCK_order == 231)
		{
			// k_out --> pix --> c_in
			k_blk_id = wave_id_x % k_out_group;
			pix_blk_id = wave_id_x / k_out_group % pix_wave;
			c_l2_blk_id = wave_id_x / k_out_group / pix_wave;
		}
		c_blk_id = c_in_lds_group * c_l2_blk_id + tid_y;

		// calcuPosIndex()
		pos_id = (pix_blk_id * WAVE_SIZE + wave_tid_x) % in_chan_stride;
		batch_id = (pix_blk_id * WAVE_SIZE + wave_tid_x) / in_chan_stride;
		out_id = k_blk_id * k_out_maps;

		// calcuOffset()
		if (batch_id >= N)
		{
			goto L_END_PRG;
		}

		wei_off = (out_id * wei_chan_stride) + (c_blk_id * c_in_maps);
		gbl_in_off = (batch_id * in_batch_stride) + (c_blk_id * c_in_maps * in_chan_stride) + pos_id;
		gbl_out_off = (batch_id * out_batch_stride) + (out_id * out_chan_stride) + pos_id;

		// LDS 
		//if (c_in_lds_group > 1)
		//{
		//	if (lds_method == LDS_SPLIT)
		//	{
		//		lds_space_size = group_sz.x * k_out_maps * c_in_lds_group;
		//		lds_split_id = tid_y % c_in_lds_group;
		//	}
		//	if (lds_method == LDS_ATOMIC)
		//	{
		//		lds_space_size = group_sz.x * k_out_maps;
		//		lds_split_id = 0;
		//	}
		//}

		// L2 
		//if (c_in_l2_group > 1)
		//{
		//	l2_space_size = group_sz.x * k_out_maps * c_in_l2_split_group;
		//
		//	if (l2_method == ATOMIC_SPLIT)
		//	{
		//		l2_split_id = c_l2_blk_id / c_in_l2_atomic_group;
		//	}
		//	if (l2_method == SPLIT_ATOMIC)
		//	{
		//		l2_split_id = c_l2_blk_id % c_in_l2_split_group;
		//	}
		//}

	L_END_PRG:
		test_idx1[grp] = c_blk_id;
	}

	print_index(test_idx1, "c_blk_id");
	free(test_idx1);

	return E_ReturnState::RTN_ERR;
}
void KernelWriterConv1x1::save_debug()
{
#if KERNEL_DEBUG
	op2("v_cvt_f32_u32", v_debug, v_debug);
	flat_store_dword(1, v_addr_dbg, v_debug, "off");
	op1("s_branch", l_end_prg);
#endif
}
