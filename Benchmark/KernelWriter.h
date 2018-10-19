/************************************************************************/
/* ���ﶨ������������������õ��������kernel�ĺ���							*/
/* ����group size����������б�ȵ�										*/
/* ���ֻ��Ҫinclude ProblemControl.h									*/
/************************************************************************/
#pragma once

#include "KernelWriterBasic.h"
#include "ProblemControl.h"

#include <sys/stat.h>

namespace krnelWriter
{
	class KernelWriter : public KernelWriterBasic
	{
	public:
		KernelWriter(T_ProblemConfig * probCfg, T_SolutionConfig * solCfg)
			: KernelWriterBasic(E_IsaArch::Gfx900)
		{
			problemConfig = probCfg;
			solutionConfig = solCfg;

			kernelName = solutionConfig->KernelName;
			kernelFile = solutionConfig->KernelFile;
			groupSize0 = solutionConfig->l_wk0;
			groupSize1 = solutionConfig->l_wk1;
			groupSize2 = solutionConfig->l_wk2;
		}

	public:
		void GenKernelString()
		{
			clearString();
			writeContent();

			clearString();
			writeSignature();
			writeContent();
			writeMetadata();
		}
		void SaveKernelString2File()
		{
			std::string kernelPath = "../../../Kernels/";
			
			if (access(kernelPath.c_str(), F_OK) == -1)
			{
				::mkdir(kernelPath.c_str(), 0777);
			}

			std::string SrcFileName = kernelPath + kernelFile;
			std::ofstream fout(SrcFileName, std::ios::out);
			if (!fout.is_open())
			{
				FATAL("can't open save file");
			}
			fout.write(KernelString.c_str(), KernelString.length());
			fout.close();
		}
		void PrintKernelString()
		{
			printf("/************************************************************************************/\n");
			printf("/***** START  %s KERNEL *****/\n", kernelName.c_str());
			printf("/************************************************************************************/\n");
			printf(KernelString.c_str());
			printf("/************************************************************************************/\n");
			printf("/***** END  %s KERNEL *****/\n", kernelName.c_str());
			printf("/************************************************************************************/\n");
		}

	protected:
		T_ProblemConfig * problemConfig;
		T_SolutionConfig * solutionConfig;
		std::string kernelName;
		std::string kernelFile;
		int groupSize0, groupSize1, groupSize2;

		Var * s_privateSeg;
		Var * s_kernelArg;
		Var * s_gid_x;
		Var * s_gid_y;
		Var * s_gid_z;

		Var * v_tid_x;
		Var * v_tid_y;
		Var * v_tid_z;

		Var * l_start_prog;
		Var * l_end_prg;

		/************************************************************************/
		/* kernel�ļ����ɺ���                                                    */
		/************************************************************************/
		void writeSignature()
		{
			setTable(0);
			wrLine(".hsa_code_object_version 2, 1");
			wrLine(".hsa_code_object_isa 9, 0, 0, \"AMD\", \"AMDGPU\"");
			wrLine("");
			wrLine(".text");
			wrLine(".globl " + kernelName);
			wrLine(".p2align 8");
			wrLine(".type " + kernelName + ",@function");
			wrLine(".amdgpu_hsa_kernel " + kernelName);
			wrLine("");
		}
		void writeContent()
		{
			initialDefaultGprs();
			setTable(0);
			wrLine(kernelName + ":");
			writeCodeObj();
			_writeProgram();
		}
		virtual void writeMetadata()
		{
			setTable(0);
			wrLine(".amd_amdgpu_hsa_metadata");
			wrLine("{ Version: [1, 0],");
			wrLine("  Kernels :");
			wrLine("    - { Name: " + kernelName + ",");
			wrLine("        SymbolName: " + kernelName + ",");
			wrLine("        Language: OpenCL C, LanguageVersion: [ 1, 2 ],");
			wrLine("        Attrs: { ReqdWorkGroupSize: [ " + d2s(groupSize0) + ", " + d2s(groupSize1) + ", " + d2s(groupSize2) + " ] }");
			wrLine("        CodeProps: { KernargSegmentSize: 24, GroupSegmentFixedSize : 0, PrivateSegmentFixedSize : 0, KernargSegmentAlign : 8, WavefrontSize : 64, MaxFlatWorkGroupSize : 512 }");
			wrLine("        Args:");
			wrLine("        - { Name: d_in  , Size : 8, Align : 8, ValueKind : GlobalBuffer, ValueType : F32, TypeName : 'float*', AddrSpaceQual : Global, IsConst : true }");
			wrLine("        - { Name: d_wei , Size : 8, Align : 8, ValueKind : GlobalBuffer, ValueType : F32, TypeName : 'float*', AddrSpaceQual : Global, IsConst : true }");
			wrLine("        - { Name: d_out , Size : 8, Align : 8, ValueKind : GlobalBuffer, ValueType : F32, TypeName : 'float*', AddrSpaceQual : Global  }");
			wrLine("      }");
			wrLine("}");
			wrLine(".end_amd_amdgpu_hsa_metadata");
			wrLine("");
		}

		/************************************************************************/
		/* kernel �����������ɺ���                                                */
		/************************************************************************/
		void initialDefaultGprs()
		{
			s_privateSeg = newSgpr("s_privateSeg", 4);
			s_kernelArg = newSgpr("s_kernelArg", 2);
			s_gid_x = newSgpr("s_gid_x");
			s_gid_y = newSgpr("s_gid_y");
			s_gid_z = newSgpr("s_gid_z");

			v_tid_x = newVgpr("v_tid_x");
			v_tid_y = newVgpr("v_tid_y");

			l_start_prog = newLaber("START_PROG");
			l_end_prg = newLaber("END_PROG");
		}
		void writeCodeObj()
		{
			setTable(1);
			wrLine(".amd_kernel_code_t");
			indent();
			wrLine("enable_sgpr_private_segment_buffer = 1");
			wrLine("enable_sgpr_kernarg_segment_ptr = 1");
			wrLine("enable_sgpr_workgroup_id_x = 1");
			wrLine("enable_sgpr_workgroup_id_y = 1");
			wrLine("enable_sgpr_workgroup_id_z = 1");
			wrLine("enable_vgpr_workitem_id = 2");
			wrLine("is_ptr64 = 1");
			wrLine("float_mode = 240");
			wrLine("granulated_wavefront_sgpr_count = " + d2s((sgprCountMax - 1) / 4));
			wrLine("granulated_workitem_vgpr_count = " + d2s((vgprCountMax - 1) / 4));
			wrLine("user_sgpr_count = 6");
			wrLine("wavefront_sgpr_count = " + d2s(sgprCountMax));
			wrLine("workitem_vgpr_count = " + d2s(vgprCountMax));
			wrLine("kernarg_segment_byte_size = 56");
			wrLine("workgroup_group_segment_byte_size = " + d2s(ldsByteCount));
			backSpace();
			wrLine(".end_amd_kernel_code_t");
			wrLine("");
		}
		void _writeProgram()
		{
			setTable(0);
			wrLine(getVar(l_start_prog) + ":");
			indent();
			writeProgram();
			setTable(0);
			wrLine(getVar(l_end_prg) + ":");
			indent();
			wrLine("s_endpgm\n");
			clrVar();
		}
		virtual void writeProgram() = 0;

		/************************************************************************/
		/* ����kernel����														 */
		/************************************************************************/
		void f_linear_addr(Var * s_base_addr, Var * v_addr)
		{
			Var * v_tmp1 = newVgpr("v_tmp1");
			Var * v_tmp2 = newVgpr("v_tmp2");

			op3("v_lshlrev_b32", v_tmp1, log2(groupSize0), s_gid_x);
			op4("v_add_lshl_u32", v_tmp1, v_tmp1, v_tid_x, 2);

			op2("v_mov_b32", v_tmp2, *s_base_addr + 1);
			op4("v_add_co_u32", v_addr, "vcc", s_base_addr, v_tmp1);
			op5("v_addc_co_u32", *v_addr + 1, "vcc", 0, v_tmp2, "vcc");

			delVar(v_tmp1);
			delVar(v_tmp2);
		}

		void f_signal_slot_addr(Var * s_signal_slot_addr, Var * s_ptr_signal, uint slot_size_per_cu)
		{
			// ��ȡӲ��ID
			Var * s_tmp1 = newSgpr("s_tmp1");
			Var * s_cu_id = newSgpr("s_cu_id");
			Var * s_se_id = newSgpr("s_se_id");
			f_read_hw_reg_hw_id("off", "off", "off", s_cu_id, "off", s_se_id, "off", "off", "off", "off", "off");
			op3("s_lshl_b32", s_tmp1, s_se_id, log2(CU_PER_SE));
			op3("s_add_u32", s_cu_id, s_tmp1, s_cu_id);

			// ����HW_CU_ID����ÿ��CU���źŲ��׵�ַ
			op3("s_lshl_b32", s_tmp1, s_cu_id, log2(slot_size_per_cu) + 2);
			op3("s_add_u32", s_signal_slot_addr, s_ptr_signal, s_tmp1);
			op3("s_addc_u32", *s_signal_slot_addr + 1, *s_ptr_signal + 1, 0);

			delVar(s_cu_id);
			delVar(s_se_id);
			delVar(s_tmp1);
		}
		void f_init_signal_slot(Var * s_signal_slot_addr, Var * v_signal_addr, uint wave_num_offset, uint signal_offset)
		{
			Var * v_tmp1 = newVgpr("v_tmp1");
			Var * v_tmp2 = newVgpr("v_tmp2");
			Var * s_tmp1 = newSgpr("s_tmp1");
			Var * s_wave_id = newSgpr("s_wave_id");
			Var * s_sig_idx = newSgpr("s_sig_idx");
			Var * l_end_init = newLaber("END_INIT");

			// ʹ��WAVE0����ʼ��(������ǰ��)(��Ҫд��L2����Ϊatomic����)
			op3("s_lshr_b32", s_wave_id, s_gid_x, log2(CU_NUM));
			op2("s_cmp_eq_u32", s_wave_id, 0);
			op1("s_cbranch_scc0", l_end_init);
			op2("s_mov_b32", s_tmp1, 0);
			s_store_dword(1, s_tmp1, s_signal_slot_addr, 0, true);
			wrLaber(l_end_init);

			// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
			// �����ʼ���ȽϿ���,��Ҫ�ṩ���,�Ա�֤��ʼ�����
			op1("s_sleep", 16);
			// ���ڲ��Եĵȴ�������ʹ��s_sleep��
			op1("s_nop", 100);
			// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

			// ����WAVE��,��ȡ�ź��±�
			op2("s_mov_b32", s_sig_idx, 1);
			s_atomic_op(E_OpType::OP_ADD, s_sig_idx, s_signal_slot_addr, wave_num_offset * 4, true);
			s_wait_lgkmcnt(0);

			// �����ź��±�����źŵ�ַ
			op3("v_lshlrev_b32", v_tmp2, 2, s_sig_idx);
			op2("v_mov_b32", v_tmp1, *s_signal_slot_addr + 1);
			op4("v_add_co_u32", v_signal_addr, "vcc", s_signal_slot_addr, v_tmp2);
			op5("v_addc_co_u32", *v_signal_addr + 1, "vcc", 0, v_tmp1, "vcc");
			
			// ��ʼ���ź�(����Ҫд��L2). �Ƿ���Ҫֻһ��thread����???????????
			op2("v_mov_b32", v_tmp1, 0);
			flat_store_dword(1, v_signal_addr, v_tmp1, "off", signal_offset * 4);
			s_wait_vmcnt(0);

			delVar(v_tmp1);
			delVar(v_tmp2);
			delVar(s_tmp1);
			delVar(s_wave_id);
			delVar(s_sig_idx);
			delVar(l_end_init);
		}
		void f_deinit_signal_slot(Var * s_signal_slot_addr, uint wave_num_offset)
		{
			Var * s_tmp1 = newSgpr("s_tmp1");

			// ����WAVE��
			op2("s_mov_b32", s_tmp1, 1);
			s_atomic_op(E_OpType::OP_SUB, s_tmp1, s_signal_slot_addr, wave_num_offset * 4, true);
			s_wait_lgkmcnt(0);

			delVar(s_tmp1);
		}
		void f_send_signal(Var * v_signal_addr, Var * v_signal, uint signal_offset)
		{
			// ���ｫ�����źŵ�waitcnt��������,Ϊ�˷�ֹ�����L1��ƹ�Ҳ���,���waitcnt����
			flat_store_dword(1, v_signal_addr, v_signal, "off", signal_offset * 4);
			s_wait_vmcnt(0);
		}
		void f_s_pend_signal(Var * s_signal_slot_addr,
			Var * l_begin_loop, Var * l_end_loop, 
			uint wave_num_offset,uint signal_offset,
			Var * s_signal)
		{
			Var * v_tmp1 = newVgpr("v_tmp1");
			Var * v_tmp2 = newVgpr("v_tmp2");
			Var * v_signal = newVgpr("v_signal");
			Var * v_signal_addr = newVgpr("v_signal_addr", 2, 2);
			Var * v_fetch_flag = newVgpr("v_fetch_flag");
			Var * v_fetch_idx_stack = newVgpr("v_fetch_idx_stack");
			Var * s_exec_save = newSgpr("s_exec_save");
			Var * s_wave_num = newSgpr("s_wave_num");
			Var * s_old_fetch = newSgpr("s_old_fetch");
			Var * s_new_fetch = newSgpr("s_new_fetch");
			Var * s_fetched_data_flag = newSgpr("s_fetched_data_flag");

			op3("v_lshlrev_b32", v_tmp1, 2, v_tid_x);
			op2("v_mov_b32", v_tmp2, *s_signal_slot_addr + 1);
			op4("v_add_co_u32", v_signal_addr, "vcc", s_signal_slot_addr, v_tmp1);
			op5("v_addc_co_u32", *v_signal_addr + 1, "vcc", 0, v_tmp2, "vcc");

			// ������32��thread,��ֻ��32��wave��prefetch
			op2("s_mov_b32", "exec_hi", 0);

			op2("s_mov_b32", s_exec_save, "exec_lo");
			op2("v_mov_b32", v_fetch_idx_stack, 0);						// fetch��Ŷ�ջ����
			op2("s_mov_b32", s_fetched_data_flag, 0);					// ���д��ڵ�fetch��־λ����

			wrLaber(l_begin_loop);
			op2("s_mov_b32", "exec_lo", s_exec_save);
			//op3("s_add_u32", s_loop_cnt2, s_loop_cnt2, 1);
			// ʹfetch�̲߳�Ƶ������
			op1("s_sleep", 10);

			// ��ȡwave��(���atomic���ı�SQC,����Ҫ��L2��ȡ)(δ��ȫ����)
			s_load_dword(1, s_wave_num, s_signal_slot_addr, wave_num_offset * 4);
			//s_load_dword(1, s_wave_num, s_signal_slot_addr, wave_num_offset * 4, true);
			s_wait_lgkmcnt(0);

			// ����wave������0���˳�prefetch
			op2("s_cmp_eq_u32", s_wave_num, 0);
			op1("s_cbranch_scc1", l_end_loop);

			// ��ȡ�ź�(���)
			flat_load_dword(1, v_signal, v_signal_addr, "off", signal_offset * 4);
			s_wait_vmcnt(0);

			op3("v_lshlrev_b32", v_fetch_flag, v_signal, 1);			// �����ת��Ϊλ��־λ
			op2("s_mov_b32", s_exec_save, "exec_lo");					// ����exec
			op2("v_readfirstlane_b32", s_old_fetch, v_fetch_idx_stack);	// ��������fetche�����
			
			// �ж��յ���Ԥȡ����Ƿ�������Ŷ�ջ��
			op3("s_or_b32", s_fetched_data_flag, s_fetched_data_flag, 1);	// ����״ν����0
			op3("v_xor_b32", v_tmp1, s_fetched_data_flag, v_fetch_flag);
			op3("v_and_b32", v_tmp1, v_tmp1, v_fetch_flag);
			op3("v_cmpx_ne_u32", "vcc", v_tmp1, 0);						// �ж��Ƿ����µ���Ҫfetche�ı�־λ

			// if vcc == 0 : continue
			op2("s_cmp_eq_u32", "vcc_lo", 0);
			op1("s_cbranch_scc1", l_begin_loop);

			op2("v_readfirstlane_b32", s_new_fetch, v_signal);			// �����Ҫfetch�ĵ�һ�����
			
			// �˴��Ѿ���õ���Ҫfetch���±꣬�����ڴ˽���fetch
			// ��Ϊ��֤�����࣬�Խ�������fetch�����������

			// �Դ��ڵ�����fetch��־λ����
			op2("s_bitset0_b32", s_fetched_data_flag, s_old_fetch);
			op2("s_bitset1_b32", s_fetched_data_flag, s_new_fetch);
			op2("s_mov_b32", s_signal, s_new_fetch);

			// �Ѿ�fetch���±�Ķ�ջ������λ����
			op2("s_mov_b32", "exec_lo", s_exec_save);
			op3("v_add_u32", v_tmp1, v_tid_x, 1);
			op3("v_lshlrev_b32", v_tmp1, 2, v_tmp1);
			op3("ds_bpermute_b32", v_fetch_idx_stack, v_tmp1, v_fetch_idx_stack);
			op3("v_writelane_b32", v_fetch_idx_stack, s_new_fetch, 7);	// д�����µ�fetch�����

			delVar(v_tmp1);
			delVar(v_tmp2);
			delVar(v_signal);
			delVar(v_signal_addr);
			delVar(v_fetch_flag);
			delVar(v_fetch_idx_stack);
			delVar(s_exec_save);
			delVar(s_wave_num);
			delVar(s_old_fetch);
			delVar(s_new_fetch);
			delVar(s_fetched_data_flag);
		}
		void f_e_pend_signal(Var * l_begin_loop, Var * l_end_loop)
		{
			op1("s_branch", l_begin_loop);
			wrLaber(l_end_loop);
		}
	};
}