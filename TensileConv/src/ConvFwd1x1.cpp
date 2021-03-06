#pragma once 

#include "ConvFwd1x1.h"
#include <fstream>

using namespace TensileConv;
using namespace AutoGen;
using namespace AutoTune;

T_SaveData setKernelParam;
/************************************************************************/
/* solution ����										                    */
/************************************************************************/
#pragma region SOLUTION

ConvFwd1x1Solution::ConvFwd1x1Solution(ConvFwd1x1Problem * problem, std::string name, LogFile * file)
	: SolutionCtrlBase(problem, name, file)
{
	this->problem = problem;
	enableSearch = problem->EnSearch;
	searchMethod = problem->SearchMethod;

	d_in = d_wei = d_bias = d_out = nullptr;

	switch (searchMethod)
	{
	case AutoTune::E_SearchMethord::SEARCH_BRUTE:
		solutionName = "Brust Search";
		searchSpace = (AutoTune::SearchSpaceBase*)new AutoTune::BruteSearch();
		break;
	case AutoTune::E_SearchMethord::SEARCH_GENETIC:
		solutionName = "Genetic Search";
		searchSpace = (AutoTune::SearchSpaceBase*)new AutoTune::GeneticSearch();
		break;
	}
}

#if 1
E_ReturnState ConvFwd1x1Solution::generateSolutionParamSpace()
{
	T_SearchParam searchParam;

	searchParam.Name = "PCK_order";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(123);
	searchParam.ValueArray.push_back(132);
	searchParam.ValueArray.push_back(213);
	searchParam.ValueArray.push_back(231);
	searchParam.ValueArray.push_back(312);
	searchParam.ValueArray.push_back(321);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "c_in_lds_atomic_group";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(1);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "c_in_lds_split_group";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(1);
	searchParam.ValueArray.push_back(2);
	searchParam.ValueArray.push_back(4);
	searchParam.ValueArray.push_back(8);
	searchParam.ValueArray.push_back(16);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "c_in_l2_atomic_group";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(1);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "c_in_l2_split_group";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(1);
	searchParam.ValueArray.push_back(2);
	searchParam.ValueArray.push_back(4);
	searchParam.ValueArray.push_back(8);
	searchParam.ValueArray.push_back(16);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "k_out_maps";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(2);
	searchParam.ValueArray.push_back(4);
	searchParam.ValueArray.push_back(8);
	searchParam.ValueArray.push_back(16);
	searchParam.ValueArray.push_back(32);
	// for Baidu
//	searchParam.ValueArray.push_back(3);
//	searchParam.ValueArray.push_back(5);
//	searchParam.ValueArray.push_back(7);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "group_size_x";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(64);
	searchParam.ValueArray.push_back(128);
	searchParam.ValueArray.push_back(256);
	searchParam.ValueArray.push_back(512);
	searchParam.ValueArray.push_back(1024);
	searchSpace->AddOneSearchParam(&searchParam);

	return E_ReturnState::SUCCESS;
}
#else
E_ReturnState ConvFwd1x1Solution::generateSolutionParamSpace()
{
	T_SearchParam searchParam;

	searchParam.Name = "PCK_order";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(123);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "c_in_lds_split_group";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(1);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "c_in_l2_split_group";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(1);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "k_out_maps";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(16);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "group_size_x";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(256);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "c_in_lds_atomic_group";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(1);
	searchSpace->AddOneSearchParam(&searchParam);
	searchParam.Name = "c_in_l2_atomic_group";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(1);
	searchSpace->AddOneSearchParam(&searchParam);

	return E_ReturnState::SUCCESS;
}
#endif

E_ReturnState ConvFwd1x1Solution::getKernelParam()
{
	SolutionCtrlBase::getKernelParam();
	
	for (T_SearchParam *param : *(searchSpace->SearchParams()))
	{
		if (param->Name == "PCK_order")				kernelParam.PCK_order = param->CurrValue;
		if (param->Name == "c_in_lds_atomic_group")	kernelParam.c_in_lds_atomic_group = param->CurrValue;
		if (param->Name == "c_in_lds_split_group")	kernelParam.c_in_lds_split_group = param->CurrValue;
		if (param->Name == "c_in_l2_atomic_group")	kernelParam.c_in_l2_atomic_group = param->CurrValue;
		if (param->Name == "c_in_l2_split_group")	kernelParam.c_in_l2_split_group = param->CurrValue;
		if (param->Name == "k_out_maps")			kernelParam.k_out_maps = param->CurrValue;
		if (param->Name == "group_size_x")			kernelParam.group_size_x = param->CurrValue;
	}

	kernelParam.N = problem->N();
	kernelParam.C = problem->C(); kernelParam.K = problem->K();
	kernelParam.W = problem->W(); kernelParam.H = problem->H();
	kernelParam.EnBias = problem->EnBias();
	kernelParam.Relu = problem->Relu();

	d_in = d_wei = d_bias = d_out = nullptr;
	size_sig = size_l2 = size_dbg = 0;

	return E_ReturnState::SUCCESS;
}

E_ReturnState ConvFwd1x1Solution::generateKernel()
{		
	// generate kernel source
	if (rtOcl->Device()->DeviceInfo()->name == "gfx900")
	{
		kernelWriter = new KernelWriterConv1x1(kernelParam, E_IsaArch::Gfx900);
	}
	else if (rtOcl->Device()->DeviceInfo()->name == "gfx803")
	{
		kernelWriter = new KernelWriterConv1x1(kernelParam, E_IsaArch::Gfx800);
	}
	ChkErr(kernelWriter->GenKernelString());
	kernelWriter->SaveKernelString2File();

	// get back kernel info
	kernelName = kernelWriter->KernelName();
	kernelFile = kernelWriter->KernelFile();
	group_sz = kernelWriter->GroupSize();
	global_sz = kernelWriter->GlobalSize();

	size_sig = kernelWriter->SlotSize();
	if (size_sig > 0)		h_sig = (float*)malloc(size_sig * sizeof(float));
	if (size_sig > 0)		memset(h_sig, 123, size_sig * sizeof(float));	// for debug
	
	size_l2 = kernelWriter->L2Size();
	if (size_l2 > 0)		h_l2 = (float*)malloc(size_l2 * sizeof(float));

	size_dbg = kernelWriter->DebugSize();
	if (size_dbg > 0)		h_dbg = (float*)malloc(size_dbg * sizeof(float));

	delete kernelWriter;

	// build up kernel obj
	kernel = rtOcl->CreatKernel((char*)kernelFile.c_str(), kernelName.c_str(), E_ProgramType::PRO_GAS_FILE);

	return E_ReturnState::SUCCESS;
}

E_ReturnState ConvFwd1x1Solution::prepareKernelArgs()
{
	negSlop = problem->NegSlop();
	d_in = rtOcl->DevMalloc(problem->size_in * sizeof(float));
	d_wei = rtOcl->DevMalloc(problem->size_wei * sizeof(float));
	d_bias = rtOcl->DevMalloc(problem->size_bias * sizeof(float));
	d_out = rtOcl->DevMalloc(problem->size_out * sizeof(float));

	if (d_in == nullptr || d_wei == nullptr || d_bias == nullptr || d_out == nullptr)
		return E_ReturnState::RTN_ERR;

	d_sig = d_l2 = d_dbg = NULL;
	if (size_sig > 0)		d_sig = rtOcl->DevMalloc(size_sig * sizeof(float));
	if (size_l2 > 0)		d_l2 = rtOcl->DevMalloc(size_l2 * sizeof(float));
	if (size_dbg > 0)		d_dbg = rtOcl->DevMalloc(size_dbg * sizeof(float));

	if (size_sig > 0 && d_sig == nullptr)		return E_ReturnState::RTN_ERR;
	if (size_l2 > 0 && d_l2 == nullptr)			return E_ReturnState::RTN_ERR;
	if (size_dbg > 0 && d_dbg == nullptr)		return E_ReturnState::RTN_ERR;

	stream->MemCopyH2D(d_in, problem->h_in, problem->size_in * sizeof(float));
	stream->MemCopyH2D(d_wei, problem->h_wei, problem->size_wei * sizeof(float));
	stream->MemCopyH2D(d_bias, problem->h_bias, problem->size_bias * sizeof(float));
	//stream->MemCopyH2D(d_sig, h_sig, size_sig * sizeof(float));	// for debug
	stream->MemCopyH2D(d_out, problem->h_out, problem->size_out * sizeof(float)); // for debug

	kernel->SetArgs(&d_in, &d_wei, &d_out, &d_bias, &d_sig, &d_l2, &negSlop, &d_dbg);

	return E_ReturnState::SUCCESS;
}

void ConvFwd1x1Solution::getBackResult()
{
	stream->MemCopyD2H(problem->h_out, d_out, problem->size_out * sizeof(float));

	if (size_sig > 0)
		stream->MemCopyD2H(h_sig, d_sig, size_sig * sizeof(float));
	if (size_l2 > 0)
		stream->MemCopyD2H(h_l2, d_l2, size_l2 * sizeof(float));
	if (size_dbg > 0)
		stream->MemCopyD2H(h_dbg, d_dbg, size_dbg * sizeof(float));
}

void ConvFwd1x1Solution::releaseDevMem()
{
	if (d_in != nullptr) rtOcl->DevFree(d_in);
	if (d_wei != nullptr) rtOcl->DevFree(d_wei);
	if (d_out != nullptr) rtOcl->DevFree(d_out);
	if (d_bias != nullptr) rtOcl->DevFree(d_bias);

	if (size_sig > 0)		rtOcl->DevFree(d_sig);
	if (size_l2 > 0)		rtOcl->DevFree(d_l2);
	if (size_dbg > 0)		rtOcl->DevFree(d_dbg);

	if (size_sig > 0)		free(h_sig);
	if (size_l2 > 0)		free(h_l2);
	if (size_dbg > 0)		free(h_dbg);
}

E_ReturnState ConvFwd1x1Solution::getBestKernelParam()
{
	INFO("+ Serching param comb: ");

	if (enableSearch)
	{
		for (T_SearchParam *param : *(searchSpace->SearchParams()))
		{
			if (param->Name == "PCK_order")				kernelParam.PCK_order = param->BestValue;
			if (param->Name == "c_in_lds_atomic_group")	kernelParam.c_in_lds_atomic_group = param->BestValue;
			if (param->Name == "c_in_lds_split_group")	kernelParam.c_in_lds_split_group = param->BestValue;
			if (param->Name == "c_in_l2_atomic_group")	kernelParam.c_in_l2_atomic_group = param->BestValue;
			if (param->Name == "c_in_l2_split_group")	kernelParam.c_in_l2_split_group = param->BestValue;
			if (param->Name == "k_out_maps")			kernelParam.k_out_maps = param->BestValue;
			if (param->Name == "group_size_x")			kernelParam.group_size_x = param->BestValue;

			INFO("+	%s = %d", param->Name.c_str(), param->BestValue);
		}
	}
	else
	{
		kernelParam.PCK_order = setKernelParam.PCK_order;
		kernelParam.c_in_lds_atomic_group = setKernelParam.c_in_lds_atomic_group;
		kernelParam.c_in_lds_split_group = setKernelParam.c_in_lds_split_group;
		kernelParam.c_in_l2_atomic_group = setKernelParam.c_in_l2_atomic_group;
		kernelParam.c_in_l2_split_group = setKernelParam.c_in_l2_split_group;
		kernelParam.k_out_maps = setKernelParam.k_out_maps;
		kernelParam.group_size_x = setKernelParam.group_size_x;

		solutionScore.ElapsedTime = setKernelParam.elapsedSec;
	}

	kernelParam.N = problem->N();
	kernelParam.C = problem->C(); kernelParam.K = problem->K();
	kernelParam.W = problem->W(); kernelParam.H = problem->H();
	kernelParam.EnBias = problem->EnBias();
	kernelParam.Relu = problem->Relu();

	d_in = d_wei = d_bias = d_out = nullptr;
	size_sig = size_l2 = size_dbg = 0;
}
void ConvFwd1x1Solution::GetBestKernel()
{
	PrintSeperator('+');

	INFO("+ Probem: [WHCKN] = [%d,%d,%d,%d,%d]:", problem->H(), problem->W(), problem->C(), problem->K(), problem->N());
	INFO("+ Best solution: " + solutionName);
	INFO("+ Best score: %.3f(us), %.1f(GFlops), %.1f%%", solutionScore.ElapsedTime * 1e6, solutionScore.Flops*1e-9, solutionScore.Performence * 100);
	INFO("+ Search time: %.1f(sec) = %d:%d", searchElapsedSec, ((int)searchElapsedSec) / 60, ((int)searchElapsedSec) % 60);
	INFO("+ Kernel name: " + kernelName);
	INFO("+ Kernel file: " + kernelFile);
	getBestKernelParam();

	bool successFlag = false;
	if((!enableSearch)&&(setKernelParam.elapsedSec == -1)) goto NO_BEST_SOLUTION;
	if(generateKernel() != E_ReturnState::SUCCESS) goto NO_BEST_SOLUTION;
	INFO("+ group_size = %d, %d, %d", group_sz.x, group_sz.y, group_sz.z);
	INFO("+ global_size = %d, %d, %d", global_sz.x, global_sz.y, global_sz.z);
	if ((enableSearch)&&(prepareKernelArgs() != E_ReturnState::SUCCESS)) goto NO_BEST_SOLUTION;
	if ((enableSearch)&&(launchKernel() != E_ReturnState::SUCCESS)) goto NO_BEST_SOLUTION;
	if (enableSearch)	getBackResult();
	successFlag = true;
	NO_BEST_SOLUTION:
	if (enableSearch)	releaseDevMem();
	if (enableSearch)
	{
		if (successFlag == false)	solutionScore.ElapsedTime = -1;
	}
	else
	{
		solutionScore.ElapsedTime = setKernelParam.elapsedSec;
	}

	logFile->Log("Probem: [WHCKN] = [%d,%d,%d,%d,%d]:", problem->H(), problem->W(), problem->C(), problem->K(), problem->N());
	logFile->Log("%s score: %.3f(us), %.1f(GFlops), %.1f%%", solutionName.c_str(), solutionScore.ElapsedTime * 1e6, solutionScore.Flops*1e-9, solutionScore.Performence * 100);
	logFile->Log("%s search time: %.1f(sec) = %d:%d", solutionName.c_str(),	searchElapsedSec, ((int)searchElapsedSec)/60, ((int)searchElapsedSec) % 60);

	PrintSeperator('+');

	if (enableSearch)
	{
		problem->verifyDevCompute();
		if (problem->IsVerifyPass())
		{
			T_SaveData saveParam;
			saveParam.N = kernelParam.N; saveParam.C = kernelParam.C; saveParam.K = kernelParam.K;
			saveParam.W = kernelParam.W; saveParam.H = kernelParam.H;
			saveParam.bias = kernelParam.EnBias; saveParam.relu = kernelParam.Relu;
			saveParam.PCK_order = kernelParam.PCK_order;
			saveParam.c_in_lds_atomic_group = kernelParam.c_in_lds_atomic_group;
			saveParam.c_in_lds_split_group = kernelParam.c_in_lds_split_group;
			saveParam.c_in_l2_atomic_group = kernelParam.c_in_l2_atomic_group;
			saveParam.c_in_l2_split_group = kernelParam.c_in_l2_split_group;
			saveParam.k_out_maps = kernelParam.k_out_maps;
			saveParam.group_size_x = kernelParam.group_size_x;
			saveParam.elapsedSec = solutionScore.ElapsedTime;

			problem->db->SaveRcd(saveParam);
		}
	}
}

#pragma endregion

/************************************************************************/
/* solver ����															*/
/************************************************************************/
#pragma region SOLVER

ConvFwd1x1Solver::ConvFwd1x1Solver(ConvFwd1x1Problem * problem, LogFile * file)
	: SolverCtrlBase(problem,file) 
{ 
	this->problem = problem;
}
void ConvFwd1x1Solver::generateSolver()
{
	EnSearch = problem->EnSearch;

	ConvFwd1x1Solution * solution = new ConvFwd1x1Solution((ConvFwd1x1Problem*)problem, "", logFile);
	solutionList->push_back(solution);
}

#pragma endregion

/************************************************************************/
/* problem ����															*/
/************************************************************************/
#pragma region PROBLEM

ConvFwd1x1Problem::ConvFwd1x1Problem(std::string name, LogFile * file)
	:ProblemCtrlBase(name, file)
{
	solver = new ConvFwd1x1Solver(this, logFile);
	
	RuntimeOCL * rtOcl = RuntimeOCL::GetInstance();
	std::string dbFileName  = "conv1x1_fwd_dir_" + rtOcl->Device()->DeviceInfo()->name;
	db = new Database(dbFileName);
	db->LoadDbFile();
}

void ConvFwd1x1Problem::generateProblem()
{
	T_SearchParam * searchParam = new T_SearchParam();

	searchParam->Name = "C";
	searchParam->ValueArray.push_back(64);
	searchParam->ValueArray.push_back(128);
	searchParam->ValueArray.push_back(256);
	searchParam->ValueArray.push_back(512);
	searchParam->ValueArray.push_back(1024);
	searchParam->ValueArray.push_back(2048);
	searchSpace->AddOneSearchParam(searchParam);

	searchParam->Name = "K";
	searchParam->ValueArray.push_back(64);
	searchParam->ValueArray.push_back(128);
	searchParam->ValueArray.push_back(256);
	searchParam->ValueArray.push_back(512);
	searchParam->ValueArray.push_back(1024);
	searchParam->ValueArray.push_back(2048);
	searchSpace->AddOneSearchParam(searchParam);

	searchParam->Name = "N";
	searchParam->ValueArray.push_back(1);
	searchParam->ValueArray.push_back(2);
	searchParam->ValueArray.push_back(4);
	searchParam->ValueArray.push_back(8);
	searchParam->ValueArray.push_back(16);
	searchParam->ValueArray.push_back(32);
	searchSpace->AddOneSearchParam(searchParam);

	searchParam->Name = "WH";
	searchParam->ValueArray.push_back(7);
	searchParam->ValueArray.push_back(14);
	searchParam->ValueArray.push_back(28);
	searchParam->ValueArray.push_back(56);
	searchParam->ValueArray.push_back(112);
	searchSpace->AddOneSearchParam(searchParam);

	searchParam->Name = "UV";
	searchParam->ValueArray.push_back(0);
	searchParam->ValueArray.push_back(1);
	searchSpace->AddOneSearchParam(searchParam);

	enBias = false;
	relu = E_Relu::NORELU;
}

void ConvFwd1x1Problem::TuneProblem()
{
	generateProblem();
	RunProblem();
}

void ConvFwd1x1Problem::TuneProblem(int W, int H, int C, int K, int N, int UV, bool isBias, int Relu, int TuneMethod)
{
	T_SearchParam searchParam;

	searchParam.Name = "W";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(W);
	searchSpace->AddOneSearchParam(&searchParam);
	searchParam.Name = "H";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(H);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "C";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(C);
	searchSpace->AddOneSearchParam(&searchParam);
	searchParam.Name = "K";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(K);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "N";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(N);
	searchSpace->AddOneSearchParam(&searchParam);

	searchParam.Name = "UV";
	searchParam.ValueArray.clear();
	searchParam.ValueArray.push_back(UV);
	searchSpace->AddOneSearchParam(&searchParam);
	
	enBias = isBias;
	relu = (E_Relu)Relu;

	std::string key = db->GenKeyStr(N, C, H, W, K, isBias, relu);
	std::map<std::string, T_SaveData>::iterator it;
	setKernelParam = db->Find(key);
	
	EnSearch = true;
	SearchMethod = E_SearchMethord::SEARCH_GENETIC;
	if (TuneMethod == SEARCH_NONE)
	{
		EnSearch = false;
	}
	else if (TuneMethod == SEARCH_AUTO)
	{
		if (setKernelParam.elapsedSec >0)
		{
			EnSearch = false;
		}
	}
	else if (TuneMethod == SEARCH_BRUTE)
	{
		SearchMethod = E_SearchMethord::SEARCH_BRUTE;
	}
	else if (TuneMethod == SEARCH_GENETIC)
	{
		SearchMethod = E_SearchMethord::SEARCH_GENETIC;
	}

	if ((C % 4 != 0) || (K % 2 != 0))
	{
		EnSearch = false;
		SearchMethod = E_SearchMethord::SEARCH_BRUTE;
		setKernelParam.elapsedSec = -1;
	}
	if (EnSearch == false)
	{
		isVeryfyPass = true;
	}

	RunProblem();
}

void ConvFwd1x1Problem::initHostParam()
{
	ProblemCtrlBase::initHostParam();
	
	for (T_SearchParam *param : *(searchSpace->SearchParams()))
	{
		if (param->Name == "N")
		{
			batch = param->CurrValue;
		}
		if (param->Name == "W")
		{
			in_width = param->CurrValue;
		}
		if (param->Name == "H")
		{
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

	PrintSeperator('*');
	INFO("* WHCKN=[%d * %d, %d, %d, %d]", in_width, in_height, in_chan, out_chan, batch);
	INFO("* stride=[%d, %d]", stride_x, stride_y);
	INFO("* Bias = %s", enBias ? "True" : "False");
	INFO("* Relu = %s", relu == NORELU ? "No Relu" : relu == RELU ? "Relu" : "PRelu");
	PrintSeperator('*');

	out_width = in_width + pad_x * 2 - wei_width + 1;
	out_height = in_height + pad_y * 2 - wei_height + 1;

	size_in = in_width * in_height * in_chan * batch;
	size_wei = wei_width * wei_height * in_chan * out_chan;
	size_bias = out_chan;
	size_out = in_width * in_height * out_chan * batch;
	
	if (!EnSearch) 
		return;

	h_in = (float*)malloc(size_in * sizeof(float));
	h_wei = (float*)malloc(size_wei * sizeof(float));
	h_bias = (float*)malloc(size_bias * sizeof(float));
	h_out = (float*)malloc(size_out * sizeof(float));
	out_ref = (float*)malloc(size_out * sizeof(float));

	LOG("input  WHCN = %d, %d, %d, %d", in_width, in_height, in_chan, batch);
	LOG("weight WHCK = %d, %d, %d, %d", wei_width, wei_height, in_chan, out_chan);
	LOG("output WHKN = %d, %d, %d, %d", out_width, out_height, out_chan, batch);
	LOG("init tensor input  = %d = %.2f MB", size_in * sizeof(float), size_in * sizeof(float) / 1024 / 1024.0);
	LOG("init tensor weight = %d = %.2f KB", size_wei * sizeof(float), size_wei * sizeof(float) / 1024.0);
	LOG("init tensor output = %d = %.2f MB", size_out * sizeof(float), size_out * sizeof(float) / 1024 / 1024.0);

	negSlop = -1.23;
	for (int i = 0; i < size_in; i++)
	{
		h_in[i] = 1;
		//h_in[i] = (float)(i % 7) + 1.0f;
		h_in[i] = (float)(rand() % 100 - 50);
		//h_in[i] = (double)rand() * (1.0 / RAND_MAX);
	}
	for (int i = 0; i < size_wei; i++)
	{
		h_wei[i] = 1;
		//h_wei[i] = (float)(i % 3);
		h_wei[i] = (float)(rand() % 100 - 50);
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
}

void ConvFwd1x1Problem::caculateTheoryPerformance()
{
	int cuNum = rtOcl->Device()->DeviceInfo()->cuNum;
	double freq = rtOcl->Device()->DeviceInfo()->freqMHz * 1e6;

	calculation = 1.0 * in_width * in_height * in_chan * batch * out_chan * wei_width * wei_height / stride_x / stride_y * 2.0;
	theoryElapsedTime = calculation / (SIMD_PER_CU * cuNum * freq * 2.0);

	LOG("Calculation = %.3f(G), TheoryElapsedTime = %.3f(us)", calculation * 1e-9, theoryElapsedTime * 1e6);
}

void ConvFwd1x1Problem::runHostCompute()
{
	if (!CPU_VERIFY)
		return;

	ProblemCtrlBase::runHostCompute();

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
					if (relu == NORELU)
					{
						out_ref[o * stride_n_out + w * stride_k_out + i * out_width + j] = acc;
					}
					else if (relu == RELU)
					{
						if (acc > 0)
						{
							out_ref[o * stride_n_out + w * stride_k_out + i * out_width + j] = acc;
						}
						else
						{
							out_ref[o * stride_n_out + w * stride_k_out + i * out_width + j] = 0;
						}
					}
					else if (relu == PRELU)
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
				}
			}
		}
	}
}

void ConvFwd1x1Problem::verifyDevCompute()
{
	if (!CPU_VERIFY)
		return;

	ProblemCtrlBase::verifyDevCompute();

	float diff = 0;
	for (int i = 0; i < size_out; i++)
	{
		diff += (out_ref[i] - h_out[i]) * (out_ref[i] - h_out[i]);
	}
	diff /= size_out;

	if (!(diff >= 0 && diff < MIN_FP32_ERR))
	{
		WARN("verify failed! mean err = %.2f", diff);
		isVeryfyPass = false;
		return;
	}

	isVeryfyPass = true;
	LOG("verify success. mean err = %.1f", diff);
}

void ConvFwd1x1Problem::releaseHostParam()
{
	ProblemCtrlBase::releaseHostParam();

	free(h_in);
	free(h_wei);
	free(h_bias);
	free(h_out);
	free(out_ref);
}

#pragma endregion
