#pragma once 

#include "ConvFwd1x1.h"

using namespace TensileConv;
using namespace AutoGen;
using namespace AutoTune;

/************************************************************************/
/* solution ����										                    */
/************************************************************************/
#pragma region SOLUTION

ConvFwd1x1Solution::ConvFwd1x1Solution(ConvFwd1x1Problem * problem)
	: SolutionCtrlBase(problem)
{	
	this->problem = problem;

	solutionName = "TensileConv";

	kernelParam.c_in_group = 2;
	kernelParam.c_in_lds_group = 2;
	kernelParam.k_out_maps = 16;
	kernelParam.group_size = 64;
}

E_ReturnState ConvFwd1x1Solution::generateSolutionParamSpace()
{
	T_SearchParam * searchParam;
	
	searchParam = new T_SearchParam("c_in_group");
	searchParam->ValueArray.push_back(1);
//	searchParam->ValueArray.push_back(2);
	searchParam->ValueArray.push_back(4);
//	searchParam->ValueArray.push_back(8);
//	searchParam->ValueArray.push_back(16);
//	searchParam->ValueArray.push_back(32);
	solutionParamSpace->AddOneParam(searchParam);
	//--------------------------------
	searchParam = new T_SearchParam("k_out_maps");
	searchParam->ValueArray.push_back(2);
	searchParam->ValueArray.push_back(4);
	searchParam->ValueArray.push_back(8);
	searchParam->ValueArray.push_back(16);
	searchParam->ValueArray.push_back(32);
	solutionParamSpace->AddOneParam(searchParam);
	//--------------------------------
	searchParam = new T_SearchParam("group_size");
	searchParam->ValueArray.push_back(64);
	searchParam->ValueArray.push_back(128);
	searchParam->ValueArray.push_back(256);
	searchParam->ValueArray.push_back(512);
	solutionParamSpace->AddOneParam(searchParam);

	return E_ReturnState::SUCCESS;
}

E_ReturnState ConvFwd1x1Solution::getKernelParam()
{
	while (true)
	{
		T_SearchParam * param = solutionParamSpace->GetOneParam();

		if (param == NULL)
		{
			break;
		}

		if (param->Name == "c_in_group")
		{
			kernelParam.c_in_group = param->CurrValue;
		}
		if (param->Name == "k_out_maps")
		{
			kernelParam.k_out_maps = param->CurrValue;
		}
		if (param->Name == "group_size")
		{
			kernelParam.group_size = param->CurrValue;
		}
	}
}

E_ReturnState ConvFwd1x1Solution::getBestKernelParam()
{
	OUTPUT("+ Serching param comb: ");

	while (true)
	{
		T_SearchParam * param = solutionParamSpace->GetOneParam();

		if (param == NULL)
		{
			break;
		}

		if (param->Name == "c_in_group")
		{
			kernelParam.c_in_group = param->BestValue;
		}
		if (param->Name == "k_out_maps")
		{
			kernelParam.k_out_maps = param->BestValue;
		}
		if (param->Name == "group_size")
		{
			kernelParam.group_size = param->BestValue;
		}
		OUTPUT("+	%s = %d", param->Name.c_str(), param->BestValue);
	}
}

E_ReturnState ConvFwd1x1Solution::generateKernel()
{	
	kernelParam.c_in_maps = problem->C() / kernelParam.c_in_group;
	kernelParam.k_out_group = _divCeil(problem->K(), kernelParam.k_out_maps);

	kernelParam.c_in_maps_once = 8;
	kernelParam.pix_per_group = 64;
	kernelParam.pix_group = _divCeil(problem->W() * problem->H() * problem->N(), kernelParam.group_size);
	kernelParam.align = kernelParam.pix_group * kernelParam.group_size;

	problem->size_sig = kernelParam.pix_group * kernelParam.k_out_group;
	problem->h_sig = (float*)malloc(problem->size_sig * sizeof(float));

#if KERNEL_DEBUG
	problem->size_dbg = kernelParam.align * kernelParam.c_in_group * kernelParam.k_out_group;
	problem->h_dbg = (float*)malloc(problem->size_dbg * sizeof(float));
#endif

	PRINT_SEPARATOR3();
	OUTPUT("- Kernel Param:");
	OUTPUT("- 	c_in_maps = %d, \tc_in_group = %d", kernelParam.c_in_maps, kernelParam.c_in_group);
	OUTPUT("- 	k_out_maps = %d, \tk_out_group = %d", kernelParam.k_out_maps, kernelParam.k_out_group);
	OUTPUT("- 	align = %d, \t\tpix_group = %d", kernelParam.align, kernelParam.pix_group);
	OUTPUT("- 	group_size = %d", kernelParam.group_size);
	PRINT_SEPARATOR3();
	
	// set up work size
	global_sz.x = kernelParam.align * kernelParam.c_in_group * kernelParam.k_out_group / kernelParam.c_in_lds_group;
	global_sz.y = kernelParam.c_in_lds_group;
	group_sz = dim3(kernelParam.group_size, kernelParam.c_in_lds_group, 1);
	
	// generate kernel source
	kernelName = "ConvFwd1x1";
	if(rtOcl->Device()->DeviceInfo()->name == "gfx900")
		kernelWriter = new KernelWriterConv1x1(problem, this, E_IsaArch::Gfx900);
	else if (rtOcl->Device()->DeviceInfo()->name == "gfx803")
		kernelWriter = new KernelWriterConv1x1(problem, this, E_IsaArch::Gfx800);

	kernelWriter->GenKernelString();
	kernelWriter->SaveKernelString2File();

	// build up kernel obj
	kernel = rtOcl->CreatKernel(
		(char *)kernelWriter->KernelFile().c_str(), kernelWriter->KernelName().c_str(), E_ProgramType::PRO_GAS_FILE);

	kernelFile = kernelWriter->KernelFile();

	return E_ReturnState::SUCCESS;
}

E_ReturnState ConvFwd1x1Solution::prepareKernelArgs()
{	
	d_in = rtOcl->DevMalloc(problem->size_in * sizeof(float));
	d_wei = rtOcl->DevMalloc(problem->size_wei * sizeof(float));
	d_bias = rtOcl->DevMalloc(problem->size_bias * sizeof(float));
	d_out = rtOcl->DevMalloc(problem->size_out * sizeof(float));
	d_sig = rtOcl->DevMalloc(problem->size_sig * sizeof(float));
	negSlop = problem->NegSlop();
#if KERNEL_DEBUG
	d_dbg = rtOcl->DevMalloc(problem->size_dbg * sizeof(float));
#endif
	
	stream->MemCopyH2D(d_in, problem->h_in, problem->size_in * sizeof(float));
	stream->MemCopyH2D(d_wei, problem->h_wei, problem->size_wei * sizeof(float));
	stream->MemCopyH2D(d_bias, problem->h_bias, problem->size_bias * sizeof(float));

	kernel->SetArgs(&d_in, &d_wei, &d_bias, &d_out, &d_sig, &negSlop, &d_dbg);

	return E_ReturnState::SUCCESS;
}

void ConvFwd1x1Solution::getBackResult()
{
	stream->MemCopyD2H(problem->h_out, d_out, problem->size_out * sizeof(float));
	stream->MemCopyD2H(problem->h_sig, d_sig, problem->size_sig * sizeof(float));
#if KERNEL_DEBUG
	stream->MemCopyD2H(problem->h_dbg, d_dbg, problem->size_dbg * sizeof(float));
#endif
}

void ConvFwd1x1Solution::releaseDevMem()
{
	rtOcl->DevFree(d_in);
	rtOcl->DevFree(d_wei);
	rtOcl->DevFree(d_bias);
	rtOcl->DevFree(d_out);
}

void ConvFwd1x1Solution::GetBestKernel()
{
	PRINT_SEPARATOR('+');
	OUTPUT("+ Probem: [WHCKN] = [%d,%d,%d,%d,%d]:", problem->H(), problem->W(), problem->C(), problem->K(), problem->N());
	OUTPUT("+ Best solution: " + solutionName);
	OUTPUT("+ Best score: %.3f (us) = %.1f%%.", solutionScore.ElapsedTime * 1e6, solutionScore.Performence * 100);
	OUTPUT("+ Kernel name: " + kernelName);
	OUTPUT("+ Kernel file: " + kernelFile);
	OUTPUT("+ group_size = [%d, %d, %d].", group_sz.x, group_sz.y, group_sz.z);
	OUTPUT("+ global_size = [%d, %d, %d].", global_sz.x, global_sz.y, global_sz.z);
	getBestKernelParam();
	PRINT_SEPARATOR('+');

	generateKernel();
	prepareKernelArgs();
	launchKernel();
	getBackResult();
	releaseDevMem();
}

#pragma endregion

/************************************************************************/
/* solver ����															*/
/************************************************************************/
#pragma region SOLVER

ConvFwd1x1Solver::ConvFwd1x1Solver(ConvFwd1x1Problem * problem)
	: SolverCtrlBase(problem)
{
	this->problem = problem;
}

void ConvFwd1x1Solver::generateSolver()
{
	ConvFwd1x1Solution * solution = new ConvFwd1x1Solution((ConvFwd1x1Problem*)problem);	
	solutionList->push_back(solution);
}

#pragma endregion

/************************************************************************/
/* problem ����															*/
/************************************************************************/
#pragma region PROBLEM


void ConvFwd1x1Problem::generateProblem()
{
	T_SearchParam * searchParam;

	searchParam = new T_SearchParam("C");
	searchParam->ValueArray.push_back(64);
	searchParam->ValueArray.push_back(128);
	searchParam->ValueArray.push_back(256);
	searchParam->ValueArray.push_back(512);
	searchParam->ValueArray.push_back(1024);
	searchParam->ValueArray.push_back(2048);
	problemParamSpace->AddOneParam(searchParam);
	searchParam = new T_SearchParam("K");
	searchParam->ValueArray.push_back(64);
	searchParam->ValueArray.push_back(128);
	searchParam->ValueArray.push_back(256);
	searchParam->ValueArray.push_back(512);
	searchParam->ValueArray.push_back(1024);
	searchParam->ValueArray.push_back(2048);
	problemParamSpace->AddOneParam(searchParam);
	searchParam = new T_SearchParam("N");
	searchParam->ValueArray.push_back(1);
	searchParam->ValueArray.push_back(2);
	searchParam->ValueArray.push_back(4);
	searchParam->ValueArray.push_back(8);
	searchParam->ValueArray.push_back(16);
	searchParam->ValueArray.push_back(32);
	problemParamSpace->AddOneParam(searchParam);
	searchParam = new T_SearchParam("WH");
	searchParam->ValueArray.push_back(7);
	searchParam->ValueArray.push_back(14);
	searchParam->ValueArray.push_back(28);
	searchParam->ValueArray.push_back(56);
	searchParam->ValueArray.push_back(112);
	problemParamSpace->AddOneParam(searchParam);
	searchParam = new T_SearchParam("UV");
	searchParam->ValueArray.push_back(0);
	searchParam->ValueArray.push_back(1);
	problemParamSpace->AddOneParam(searchParam);
}

void ConvFwd1x1Problem::TuneProblem()
{
	generateProblem();
	RunProblem();
}

void ConvFwd1x1Problem::TuneProblem(int WH, int C, int K, int N, int UV, bool isBias, bool isRelu)
{
	batch = N;
	in_width = WH;		in_height = WH;
	in_chan = C;		out_chan = K;
	stride_x = UV;		stride_y = UV;
	enBias = isBias;
	enRelu = isRelu;

	RunProblem();
}

E_ReturnState ConvFwd1x1Problem::initHostParam()
{
	while (true)
	{
		T_SearchParam * param = problemParamSpace->GetOneParam();

		if (param == NULL)
		{
			break;
		}

		if (param->Name == "N")
		{
			batch = param->CurrValue;
		}
		if (param->Name == "WH")
		{
			in_width = param->CurrValue;
			in_height = param->CurrValue;
		}
		if (param->Name == "C")
		{
			in_chan = param->CurrValue;
		}
		if (param->Name == "K")
		{
			out_chan = param->CurrValue;
		}
		if (param->Name == "UV")
		{
			stride_x = param->CurrValue;
			stride_y = param->CurrValue;
		}
	}

	// FIX Parameters
	wei_width = 1;	wei_height = 1;	// filter size
	pad_x = 0;		pad_y = 0;		// padding

	PRINT_SEPARATOR1();
	OUTPUT("* WHCKN=[%d * %d, %d, %d, %d] stride=[%d, %d]", in_width, in_height, in_chan, out_chan, batch, stride_x, stride_y);
	OUTPUT("* Bias = %s, Relu = %s", enBias ? "True" : "False", enRelu ? "True" : "False");
	PRINT_SEPARATOR1();

	out_width = in_width + pad_x * 2 - wei_width + 1;
	out_height = in_height + pad_y * 2 - wei_height + 1;

	size_in = in_width * in_height * in_chan * batch;
	size_wei = wei_width * wei_height * in_chan * out_chan;
	size_bias = out_chan;
	size_out = in_width * in_height * out_chan * batch;
	
	h_in = (float*)malloc(size_in * sizeof(float));
	h_wei = (float*)malloc(size_wei * sizeof(float));
	h_bias = (float*)malloc(size_bias * sizeof(float));
	h_out = (float*)malloc(size_out * sizeof(float));
	out_ref = (float*)malloc(size_out * sizeof(float));

	INFO("input  WHCN = [%d, %d, %d, %d]", in_width, in_height, in_chan, batch);
	INFO("weight WHCK = [%d, %d, %d, %d]", wei_width, wei_height, in_chan, out_chan);
	INFO("output WHKN = [%d, %d, %d, %d]", out_width, out_height, out_chan, batch);
	INFO("init tensor input  = %d = %.3f MByte.", size_in / sizeof(float), size_in / 1024 / 1024.0);
	INFO("init tensor weight = %d = %.3f KByte.", size_wei / sizeof(float), size_wei / 1024.0);
	INFO("init tensor output = %d = %.3f MByte.", size_out / sizeof(float), size_out / 1024 / 1024.0);

	negSlop = -1.23;
	for (int i = 0; i < size_in; i++)
	{
		h_in[i] = 1;
		//h_in[i] = (float)(i % 7) + 1.0f;
		//h_in[i] = (float)(rand() % 100 - 50);
		//h_in[i] = (double)rand() * (1.0 / RAND_MAX);
	}
	for (int i = 0; i < size_wei; i++)
	{
		h_wei[i] = 1;
		//h_wei[i] = (float)(i % 3);
		//h_wei[i] = (float)(rand() % 100 - 50);
		//h_in[i] = (double)rand() * (1.0 / RAND_MAX);
	}
	for (int i = 0; i < size_bias; i++)
	{
		h_bias[i] = 0;
		h_bias[i] = (float)(rand() % 100 - 50);
	}
	for (int i = 0; i < size_out; i++)
	{
		h_out[i] = 555.555;
	}

	return E_ReturnState::SUCCESS;
}

void ConvFwd1x1Problem::caculateTheoryPerformance()
{
	int cuNum = rtOcl->Device()->DeviceInfo()->cuNum;
	double freq = rtOcl->Device()->DeviceInfo()->freqMHz * 1e6;

	calculation = 1.0 * in_width * in_height * in_chan * batch * out_chan * wei_width * wei_height / stride_x / stride_y * 2.0;
	theoryElapsedTime = calculation / (SIMD_PER_CU * cuNum * freq * 2.0);

	INFO("Calculation = %.3f(G), TheoryElapsedTime = %.3f(us)", calculation * 1e-9, theoryElapsedTime * 1e6);
}

void ConvFwd1x1Problem::runHostCompute()
{
	int stride_n_in;			// stride for differetn batch of input
	int stride_c_in;			// stride for differetn channel in the same batch of input
	int stride_k_wei;			// stride for differetn feature of weight
	int stride_c_wei;			// stride for differetn channel in the same feature of weight
	int stride_n_out;			// stride for differetn bathc of output
	int stride_k_out;			// stride for differetn feature in the same batch of output

	int dilation_h = 1;
	int dilation_w = 1;

	stride_c_in = in_width * in_height;
	stride_n_in = in_width * in_height * in_chan;
	stride_c_wei = wei_width * wei_height;
	stride_k_wei = wei_width * wei_height * in_chan;
	stride_k_out = out_width * out_height;
	stride_n_out = out_width * out_height * out_chan;

	for (int o = 0; o < batch; o++)			// for batch size
	{
		for (int w = 0; w < out_chan; w++)		// for output features 
		{
			for (int i = 0; i < out_height; i++)		// for output heigh
			{
				for (int j = 0; j < out_width; j++)	// for output width
				{
					float acc = enBias ? h_bias[w] : 0.0;

					for (int k = 0; k < in_chan; k++)		// sum input channels
					{
						for (int x = 0; x < wei_height; x++)		// for filter heigh
						{
							for (int y = 0; y < wei_width; y++)	// for filter width
							{
								int in_off_w = j * stride_x;
								int in_off_h = i * stride_y;
								int in_x = in_off_w - pad_x + y * dilation_w;
								int in_y = in_off_h - pad_y + x * dilation_h;

								if ((in_y >= 0 && in_y < in_height) && (in_x >= 0 && in_x < in_width))
								{
									int idx_in = o * stride_n_in + k * stride_c_in + in_y * in_width + in_x;
									int idx_wei = w * stride_k_wei + k * stride_c_wei + x * wei_width + y;

									acc += h_in[idx_in] * h_wei[idx_wei];
								}
							}
						}
					}
					if (enRelu == true)
					{
						if (acc < 0)
						{
							out_ref[o * stride_n_out + w * stride_k_out + i * out_width + j] = acc * negSlop;
						}
						else
						{
							out_ref[o * stride_n_out + w * stride_k_out + i * out_width + j] = acc;
						}
					}
					else
					{
						out_ref[o * stride_n_out + w * stride_k_out + i * out_width + j] = acc;
					}
				}
			}
		}
	}
}

E_ReturnState ConvFwd1x1Problem::verifyDevCompute()
{
	float diff = 0;
	for (int i = 0; i < size_out; i++)
	{
		diff += (out_ref[i] - h_out[i]) * (out_ref[i] - h_out[i]);
	}
	diff /= size_out;

	if (!(diff >= 0 && diff < MIN_FP32_ERR))
	{
		ERR("verify failed! mean err = %.2f.", diff);
	}

	INFO("verify success. mean err = %.1f.", diff);
	return E_ReturnState::SUCCESS;
}

void ConvFwd1x1Problem::releaseHostParam()
{
	free(h_in);
	free(h_wei);
	free(h_bias);
	free(h_out);
	free(out_ref);
}

#pragma endregion
