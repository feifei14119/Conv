#pragma once

#include "KernelWriter.h"


namespace TensileConv {
namespace AutoGen {

typedef enum LdsReduceEnum
{
	LDS_SPLIT = 1,
	LDS_ATOMIC = 2
}E_LdsReduce;
typedef enum L2ReduceEnum
{
	ATOMIC_SPLIT = 1,
	SPLIT_ATOMIC = 2
}E_L2Reduce;
typedef struct Conv1x1KernelParamType
{
	int group_size_x;
	
	E_LdsReduce lds_method;
	E_L2Reduce l2_method;
	int PCK_order;
	int c_in_lds_group;
	int c_in_l2_split_group;
	int c_in_l2_atomic_group;
	int k_out_maps;

	int N, C, H, W, K;
	bool EnBias, EnRelu;
}T_Conv1x1KernelParam;

class KernelWriterConv1x1 :public AutoGen::KernelWriter
{
public:
	KernelWriterConv1x1(T_Conv1x1KernelParam kernelParam, E_IsaArch isaArch = E_IsaArch::Gfx900);
	size_t SlotSize() { return size_sig; }
	size_t DebugSize() { return size_dbg; }
	size_t L2Size() { return size_l2_split; };

protected:
	T_Conv1x1KernelParam kernelParam;

	typedef enum
	{
		PING_FLAG = 1,
		PANG_FLAG = 2
	}E_PingPang;

	// -------------------------------------------------------------------------------
	int N, C, H, W, K;
	bool enBias, enRelu;

	int PCK_order;
	E_LdsReduce lds_method;
	E_L2Reduce l2_method;
	int c_in_maps;
	int c_in_group;				// c_in_l2_group * c_in_lds_group
	int c_in_lds_group;			// c_lds_split_group * c_lds_atomic_group = group_sz.y
	int c_in_l2_group;			// c_l2_split_group * c_l2_atomic_group
	int c_in_l2_split_group;
	int c_in_l2_atomic_group;
	int	c_in_maps_once = 8;		// ����һ��ѭ���� input channel �Ļ��� [8,16]
	int c_in_maps_once_real;
	int unroll_time = 2;

	int k_out_maps;
	int k_out_group;

	size_t align;
	int pix_group;				// �������ر��ֵ�����group
	int pix_wave;				// �������ر��ֵ�����wave
	int pix_per_group = 64;

	size_t size_sig, size_dbg, size_l2_split;

	// -------------------------------------------------------------------------------
	int in_chan_stride;		// IN_CHANNEL_STRIDE
	int in_batch_stride;	// IN_BATCH_STRIDE
	int wei_chan_stride;	// WEI_CHANNEL_STRIDE
	int out_chan_stride;	// OUT_CHANNEL_STRIDE
	int out_batch_stride;	// OUT_BATCH_STRIDE
	int out_size;			
	int group_wave_num_x;	// PIX_BLK_PER_GROUP
	int conv_loop;			// LOOP
	
	bool en_input_offset;
	bool en_wei_addr_offset = true;
	int offset_grp_num;
	Var * v_global_offset;
	int wei_offset;

	// -------------------------------------------------------------------------------
	Var * s_ptr_in;
	Var * s_ptr_wei;
	Var * s_ptr_out;
	Var * s_ptr_bias;
	Var * s_ptr_sig;
	Var * s_ptr_l2;
	Var * s_slop;

	// -------------------------------------------------------------------------------
	Var * v_wave_id_x;		// ȫ��x�����wave id, ������LDS�����wave id
	Var * v_wave_tid_x;
	Var * v_pix_blk_id;
	Var * v_c_blk_id;		// c_in ȫ����ֵ�block id, ����LDS���
	Var * v_c_l2_blk_id;	// c_in ��L2��ֵ�block id, ������LDS���
	Var * v_k_blk_id;	

	Var * v_pos_id;
	Var * v_batch_id;
	Var * v_out_id;
	Var * v_l2_pos_id;		// ָ��l2_split�ĵڼ���l2
	Var * v_lds_pos_id;		// ָ��lds_split�ĵڼ���lds

	Var * v_addr_in;
	Var * s_addr_wei;
	Var * v_addr_out;
	Var * v_addr_bias;
	Var * s_addr_sig;
	Var * v_addr_lds;
	Var * v_addr_l2;
	Var * s_addr_bias;		// ???
	Var * v_addr_out_back;	// ???

	// -------------------------------------------------------------------------------
	Var * v_in_buff_a;
	Var * v_in_buff_b;
	Var * s_wei_buff_a;
	Var * s_wei_buff_b;
	Var * v_acc_buff;
	Var * s_prefetch;

	Var * s_exec_save;

	bool en_slop_zero = true;

#if KERNEL_DEBUG
	Var * v_debug;
	Var * v_debug2;
	Var * s_ptr_dbg;
	Var * v_addr_dbg;
#endif

	E_ReturnState checkKernelParam();
	E_ReturnState writeProgram()
	{
		v_debug = newVgpr("v_debug");
		v_debug2 = newVgpr("v_debug2");

		CheckFunc(checkKernelParam());
//		CheckFunc(simulate_index());
		CheckFunc(calcuIndex());
		main_conv();
		save_debug();
		
		clrVar();

		return E_ReturnState::SUCCESS;
	}

	/************************************************************************************/
	/* �������																			*/
	/************************************************************************************/
	void main_conv();
	void conv_one_loop(Var * in_buff, bool is_pang_buff);
	void conv_last_loop(Var * in_buff);
	void conv_one_accum(Var * in_buff, Var * wei_buff, Var * accum);

	/************************************************************************************/
	/* �����±�																			*/
	/************************************************************************************/
	E_ReturnState calcuIndex();
	E_ReturnState loadKernelArgs();
	E_ReturnState calcuWaveIndex();
	E_ReturnState calcuBlkIndex();
	E_ReturnState calcuPosIndex();
	E_ReturnState calcuOffset();

	/************************************************************************************/
	/* ���ݶ�ȡ��洢																		*/
	/************************************************************************************/
	void load_input(Var * in_buff);
	void load_weight(Var * wei_buff);
	void prefetch_weight();
	void init_output();

	void save_result();
	void save_to_lds_split();
	void save_to_lds_atomic();
	void save_to_l2_split();
	void save_to_l2_atomic();
	void save_to_output();
	
	void save_with_atomic(int n, Var * addr_out, Var * accum);
	void save_with_slop_zero();

	void inner_group_sync();

	E_ReturnState simulate_index();
	void save_debug();
	void print_kernel_param();
};
}
}