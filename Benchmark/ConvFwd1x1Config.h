#pragma once 

#include "BasicClass.h" 

/************************************************************************/
/* ��չ����                                                              */
/* ������Ҫ���ݸ� KernelWriterBase ����չ����								*/
/************************************************************************/
typedef struct ExtConvFwd1x1SolutionConfigTpye
{
	// K����
	int k_out_maps;		// ÿ��CU������ٸ��������ֵK

	// thread�滮
	int group_size;		// ÿ��workgroup�ж��ٸ�thread
	
	// ��������
	// c_in_maps_once:		 8:[8,16]
	// wei_pingpang_ins:	 1:[1,2,4,8]
	// en_in_pingpang:		 1:[0,1]
	// wait_cnt_in_fetch:	 4:[1,2,4,8,16]

	std::list<T_KernelArgu> * preArgus;
}T_ExtConvFwd1x1SolutionConfig;

typedef struct ExtConvFwd1x1ProblemConfigType
{
	int N;				// batch size
	int W, H;			// input size
	int C, K;			// input channel / output feature
	int X, Y;			// weight size
	int R, S;			// padding 
	int U, V;			// stride
	int OutW, OutH;		// output size

	float* h_in, *h_wei, *h_out, *out_ref;
	int size_in, size_wei, size_out;
}T_ExtConvFwd1x1ProblemConfig;