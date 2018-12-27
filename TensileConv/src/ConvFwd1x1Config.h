#pragma once 

//#include "BasicClass.h" 

/************************************************************************/
/* ��չ����                                                              */
/* ������Ҫ���ݸ� KernelWriterBase ����չ����								*/
/************************************************************************/
typedef struct ExtConvFwd1x1SolutionConfigTpye
{
	// thread�滮
	int group_size;		// ÿ��workgroup�ж��ٸ�thread
	
	// ����һ��ѭ���� input channel �Ļ���
	int c_in_maps_once;		 // 8:[8,16]

	// ����һ�� thread �� input channel ����
	int c_in_maps;
	int c_in_group;

	// ����һ�� thread �� output channel ����
	int k_out_maps;
	int k_out_group;
	
	int pix_group;

	// ����һ�� work group �� pixal ����
	int pix_per_group;
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
	bool enBias;
	bool enRelu;

	float* h_in, *h_wei, *h_bias, *h_out, *h_sig;
	float *out_ref, *h_dbg;
	float negSlop;
	int size_in, size_wei, size_bias, size_out, size_sig;
	int size_dbg;

	int N2;
	size_t sizeN;
	float * h_a, * h_b, * h_c;
}T_ExtConvFwd1x1ProblemConfig;
