﻿#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "../common/ff_utils.h"
#include "../common/ff_runtime.h"
#include "ConvFwd1x1.h"

using namespace TensileConv;

int main(int argc, char *argv[])
{
	CmdArgs * ca = new CmdArgs(argc, argv);
	RuntimeOCL * pOcl = RuntimeOCL::GetInstance();
	LogFile * logFile = new LogFile("TensileConv", false);

	bool evinfo = *(int*)ca->GetOneArg(E_ArgId::CMD_ARG_EVINFO) == 1;
	if (evinfo == true)
	{
		pOcl->PrintRuntimeInfo(true);
		delete pOcl;
		return 0;
	}

	pOcl->PrintRuntimeInfo();
	pOcl->SellectDevice(0);

	int W = *(int*)ca->GetOneArg(E_ArgId::CMD_ARG_W);
	int H = *(int*)ca->GetOneArg(E_ArgId::CMD_ARG_H);
	int C = *(int*)ca->GetOneArg(E_ArgId::CMD_ARG_C);
	int K = *(int*)ca->GetOneArg(E_ArgId::CMD_ARG_K);
	int N = *(int*)ca->GetOneArg(E_ArgId::CMD_ARG_N);
	int UV = *(int*)ca->GetOneArg(E_ArgId::CMD_ARG_UV);
	bool Bias = *(int*)ca->GetOneArg(E_ArgId::CMD_ARG_BIAS) == 1;
	int Relu = *(int*)ca->GetOneArg(E_ArgId::CMD_ARG_RELU);
	int TuneMethod = *(int*)ca->GetOneArg(E_ArgId::CMD_ARG_SEARCH);
	TuneMethod = AutoTune::E_SearchMethord::SEARCH_BRUTE;

	//W = 56; H = 56; N = 1; C = 64; K = 64; UV = 1; Bias = true; Relu = E_Relu::RELU;
	//for (N = 1; N <= 16; N *= 2)
	//{
	//	//for (C = 64; C <= 1024; C *= 2)
	//	{
	//		//for (K = 64; K <= 1024; K *= 2)
	//		{
	//			//for (H = 7; H <= 224; H *= 2)
	//			{
	//				W = H;
	//				ConvFwd1x1Problem * conv = new ConvFwd1x1Problem("DirConv1x1Fwd", logFile);
	//				conv->TuneProblem(W, H, C, K, N, UV, Bias, Relu, TuneMethod);
	//				delete conv;
	//			}
	//
	//		}
	//	}
	//}

	ConvFwd1x1Problem * conv = new ConvFwd1x1Problem("DirConv1x1Fwd", logFile);
	conv->TuneProblem(W, H, C, K, N, UV, Bias, Relu, TuneMethod);
	ConvFwd1x1Solution * slt = (ConvFwd1x1Solution*)conv->BestSolution();
	INFO("kernel file: " + slt->KernelFile());
	INFO("kernel name: " + slt->KernelName());
	INFO("signal size: %d", slt->SignalSize());
	INFO("l2 split size: %d", slt->L2SplitSize());
	INFO("debug size: %d", slt->DebugSize());
	INFO("group size: [%d, %d, %d]", slt->GroupSize().x, slt->GroupSize().y, slt->GroupSize().z);
	INFO("global size: [%d, %d, %d]", slt->GlobalSize().x, slt->GlobalSize().y, slt->GlobalSize().z);
	INFO("elapsed time: %.1f(us)", slt->SolutionScore().ElapsedTime * 1e6);

	delete pOcl;
	return 0;
}
