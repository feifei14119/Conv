/************************************************************************************/
/************************************************************************************/
.hsa_code_object_version 2,1
.hsa_code_object_isa 9,0,0,"AMD","AMDGPU"

.text
.globl ProducerConsumer
.p2align 8
.type ProducerConsumer,@function
.amdgpu_hsa_kernel ProducerConsumer

/************************************************************************************/
/* Ԥ����																			*/
/************************************************************************************/
// ==================================================================================
// ��������
// ==================================================================================
.set LOCAL_SIZE_LOG2,	6
.set VECTOR_SIZE, 64*64

// ==================================================================================
// ��������Ų�
// ==================================================================================
a_ptr_off = 0x00
b_ptr_off = 0x08
c_ptr_off = 0x10

// ==================================================================================
// SGPR �Ų�
// ==================================================================================
s_arg = 4
s_gidx = 6

s_temp0 = 7
s_a_ptr = 8
s_b_ptr = 10
s_c_ptr = 12
s_temp1 = 14
s_temp2 = 16
s_temp3 = 18
s_desc0 = 20
s_desc1 = 21
s_desc2 = 22
s_desc3 = 23
s_base_offset = 24

// ==================================================================================
// VGPR �Ų�
// ==================================================================================
v_tid0 = 0
v_a_addr = 2
v_b_addr = 4
v_c_addr = 6
v_lds_addr = 8
v_temp1 = 10
v_temp2 = 12
v_temp3 = 14
v_index = 15
v_offset = 16

.set USE_LDS, 0
.set USE_GLB, 1

/************************************************************************************/
/* ������																			*/
/************************************************************************************/
ProducerConsumer:
	// ===============================================================================
	// ===============================================================================
	.amd_kernel_code_t
		enable_sgpr_private_segment_buffer = 1
		enable_sgpr_kernarg_segment_ptr = 1
		enable_sgpr_workgroup_id_x = 1
		enable_sgpr_workgroup_id_y = 0
		enable_sgpr_workgroup_id_z = 0
		
		enable_vgpr_workitem_id = 0
	    
		is_ptr64 = 1
		float_mode = 240
		
		granulated_workitem_vgpr_count = 15
		granulated_wavefront_sgpr_count = 7
		user_sgpr_count = 6
		kernarg_segment_byte_size = 56
		wavefront_sgpr_count = 64
		workitem_vgpr_count = 64
		workgroup_group_segment_byte_size = 64*4	
	.end_amd_kernel_code_t
	
	// ===============================================================================
	// ===============================================================================
// Disassembly:        
	s_load_dwordx2		s[s_a_ptr:s_a_ptr+1], s[s_arg:s_arg+1], 0x0+a_ptr_off   
	s_load_dwordx2 		s[s_b_ptr:s_b_ptr+1], s[s_arg:s_arg+1], 0x0+b_ptr_off
	s_load_dwordx2 		s[s_c_ptr:s_c_ptr+1], s[s_arg:s_arg+1], 0x0+c_ptr_off
	v_mov_b32			v[v_lds_addr], v[v_tid0]
	s_waitcnt 			lgkmcnt(0)
	
	s_cmp_ge_u32		s[s_gidx], 0x0 + 64
	s_cbranch_scc1		CONSUMER													// if(gid >= 64) goto CONSUMER
	
PRODUCER:
	// ===============================================================================
	// ������
	// ===============================================================================
	// -------------------------------------------------------------------------------
	// ��������a�±�: 
	// a_addr = a_ptr + (gid_x * local_size) + tid_x
	// -------------------------------------------------------------------------------
	v_lshlrev_b32 		v[v_temp1], 0x0 + LOCAL_SIZE_LOG2, s[s_gidx]
	v_add_lshl_u32 		v[v_temp1], v[v_temp1], v[v_tid0], 2
		
	v_mov_b32			v[v_temp2], s[s_a_ptr+1]
	v_add_co_u32 		v[v_a_addr], vcc, s[s_a_ptr], v[v_temp1]
	v_addc_co_u32 		v[v_a_addr+1], vcc, 0, v[v_temp2], vcc
		
	// -------------------------------------------------------------------------------
	// �ȴ��ź�
	// -------------------------------------------------------------------------------
PROD_WAIT:
	s_sleep				0x10
.if USE_GLB
	s_load_dword 		s[s_temp0], s[s_b_ptr:s_b_ptr+1], 0x0 
	s_waitcnt 			lgkmcnt(0)
.endif
.if USE_LDS
	ds_read_b32			v[v_temp3], v[v_lds_addr]						offset:0x0
	s_waitcnt 			lgkmcnt(0)
	v_readfirstlane_b32	s[s_temp0], v[v_temp3]
.endif
	s_cmp_eq_u32		s[s_temp0], 0x1234
	s_cbranch_scc0		PROD_WAIT
	
	// -------------------------------------------------------------------------------
	// ��������
	// -------------------------------------------------------------------------------
	global_load_dword	v[v_temp1], v[v_a_addr:v_a_addr+1], off
	s_waitcnt			vmcnt(0)
		
	// -------------------------------------------------------------------------------
	// �����ź�
	// -------------------------------------------------------------------------------
.if USE_GLB
	s_mov_b32			s[s_temp0], 0x4321
	s_store_dword		s[s_temp0], s[s_b_ptr:s_b_ptr+1], 0x0
.endif
.if USE_LDS
	v_mov_b32			v[v_temp3], 0x4321
	ds_write_b32		v[v_lds_addr], v[v_temp3]						offset:0x0
.endif
	
	// -------------------------------------------------------------------------------
	// ����˳�
	// -------------------------------------------------------------------------------
	s_branch			END_PROG
	
CONSUMER:
	// ===============================================================================
	// ������
	// ===============================================================================
	// -------------------------------------------------------------------------------
	// ��������±�: 
	// c_addr = c_ptr + (gid_x * local_size) + tid_x
	// -------------------------------------------------------------------------------
	v_lshlrev_b32 		v[v_temp1], 0x0 + LOCAL_SIZE_LOG2, s[s_gidx]
	v_add_lshl_u32 		v[v_temp1], v[v_temp1], v[v_tid0], 2
		
	v_mov_b32			v[v_temp2], s[s_c_ptr+1]
	v_add_co_u32 		v[v_c_addr], vcc, s[s_c_ptr], v[v_temp1]
	v_addc_co_u32 		v[v_c_addr+1], vcc, 0, v[v_temp2], vcc
	
	s_sleep				0x50
	// -------------------------------------------------------------------------------
	// �����ź�
	// -------------------------------------------------------------------------------
.if USE_GLB
	s_mov_b32			s[s_temp0], 0x1234
	s_store_dword		s[s_temp0], s[s_b_ptr:s_b_ptr+1], 0x0
.endif
.if USE_LDS
	v_mov_b32			v[v_temp3], 0x1234
	ds_write_b32		v[v_lds_addr], v[v_temp3]						offset:0x0
.endif
	
	// -------------------------------------------------------------------------------
	// �ȴ��ź�
	// -------------------------------------------------------------------------------
CSM_WAIT:
	s_sleep				0x10
.if USE_GLB
	s_load_dword 		s[s_temp0], s[s_b_ptr:s_b_ptr+1], 0x0
	s_waitcnt 			lgkmcnt(0)
.endif
.if USE_LDS
	ds_read_b32			v[v_temp3], v[v_lds_addr]						offset:0x0
	s_waitcnt 			lgkmcnt(0)
	v_readfirstlane_b32	s[s_temp0], v[v_temp3]
.endif
	s_cmp_eq_u32		s[s_temp0], 0x4321
	s_cbranch_scc0		CSM_WAIT
	
	// -------------------------------------------------------------------------------
	// ��������
	// -------------------------------------------------------------------------------
	v_mov_b32			v[v_temp1],1.23
	global_store_dword	v[v_c_addr:v_c_addr+1], v[v_temp1], off
	
	// -------------------------------------------------------------------------------
	// ����˳�
	// -------------------------------------------------------------------------------
	s_branch			END_PROG	

END_PROG:	
	s_endpgm                              
	
/************************************************************************************/
/* metadata																			*/
/************************************************************************************/	 
.amd_amdgpu_hsa_metadata
{ Version: [ 1, 0 ],
  Kernels: 
    - { Name: ProducerConsumer, SymbolName: 'ProducerConsumer', Language: OpenCL C, LanguageVersion: [ 1, 2 ],
        Attrs: { ReqdWorkGroupSize: [ 1, 1, 1 ] }
        CodeProps: { KernargSegmentSize: 24, GroupSegmentFixedSize: 0, PrivateSegmentFixedSize: 0, KernargSegmentAlign: 8, WavefrontSize: 64, MaxFlatWorkGroupSize: 512 }
        Args:
        - { Name: a, Size: 8, Align: 8, ValueKind: GlobalBuffer, ValueType: F32, TypeName: 'float*', AddrSpaceQual: Global, IsConst: true }
        - { Name: b, Size: 8, Align: 8, ValueKind: GlobalBuffer, ValueType: F32, TypeName: 'float*', AddrSpaceQual: Global, IsConst: true }
        - { Name: c, Size: 8, Align: 8, ValueKind: GlobalBuffer, ValueType: F32, TypeName: 'float*', AddrSpaceQual: Global  }
      }
}
.end_amd_amdgpu_hsa_metadata
