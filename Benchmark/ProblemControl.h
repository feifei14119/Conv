#pragma once

#include <stdarg.h>
#include <vector>
#include "BasicClass.h"
#include "RuntimeControl.h"

#include "unistd.h"

/************************************************************************/
/* solution�÷�                                                         */
/************************************************************************/
typedef struct ScoreTypde
{
	double ElapsedTime;
	double Flops;
	double Performence;
}T_Score;

/************************************************************************/
/* solution ����				                                            */
/* ������Ҫ���ݸ� KernelWriterBase ��ͨ�ò���								*/
/************************************************************************/
typedef struct SolutionConfigTpye
{
	std::string ConfigName;				// �����������
	SearchSpace KernelSearchSpace;		// ����������������ռ�
	void * extConfig;

	std::string KernelName;			// kernel function name, will used to find source file
	std::string KernelFile;			// ����ָ���ļ�������ʹ��KernelName�Ƶ�.��Ҫ��׺
	std::string KernelString;
	E_KernleType KernelSrcType;
	std::string extCompilerOpt;

	size_t l_wk0, l_wk1, l_wk2;
	size_t g_wk0, g_wk1, g_wk2;
	size_t b_wk0, b_wk1, b_wk2;

	std::list<T_KernelArgu> * KernelArgus;

}T_SolutionConfig;

/************************************************************************/
/* problem ����				                                            */
/************************************************************************/
typedef struct ProblemConfigType
{
	std::string ConfigName;				// ������������
	SearchSpace ProblemParamSpace;		// ������������ռ�
	void * extConfig;

	double Calculation;					// ������
	double TheoryElapsedTime;			// ����ִ��ʱ��
}T_ProblemConfig;

/************************************************************************/
/* solution ���� (so called generic solver)			                    */
/************************************************************************/
class SolutionCtrlBase
{
public:
	SolutionCtrlBase()
	{
		RepeatTime = 100;
		SolutionConfigList = new std::list<T_SolutionConfig*>;
	}

	void RunSolution(T_ProblemConfig *problem)
	{
		ProblemConfig = problem;

		// ======================================================================
		// ���ɽ�������ռ�
		// ======================================================================
		INFO("generate solution config list.");
		SolutionConfigList->clear();
		GenerateSolutionConfigs();

		// ======================================================================
		// ����ÿ��problem��solution�����ռ�
		// ======================================================================
		std::list<T_SolutionConfig*>::iterator solutionCfgIt;
		for (solutionCfgIt = SolutionConfigList->begin();
			solutionCfgIt != SolutionConfigList->end();
			solutionCfgIt++)
		{
			SolutionConfig = *solutionCfgIt;

			printf("======================================================================\n");
			printf("Solution Name: %s.\n", SolutionConfig->ConfigName.c_str());
			printf("======================================================================\n");
			
			if (SolutionConfig->KernelSearchSpace.ParamNum > 0)
			{
				RunOneSolutionConfig();
			}
			else
			{
				RunSolutionOnce();
			}
		}
	}

	E_ReturnState RunOneSolutionConfig()
	{
		while (true)
		{
			runtime = new RuntimeCtrl(false);

			INFO("initialize device.");						InitDev();
			INFO("generate source, compiler, worksize.");	GenerateSolution();
			INFO("compiler kernel and program.");			SetupSolution();
			INFO("set arg and launch kernel.");				LaunchSolution(false);
			INFO("collect performence.");					GetPerformence();
			INFO("copy result back to cpu.");				GetBackResult();
			INFO("release device.");						ReleaseDev();
			INFO("search kernel parameters.");
			if (SolutionConfig->KernelSearchSpace.GetNexComb() == E_ReturnState::FAIL)
			{
				INFO("search kernel parameters finished.");
				ReportProblemPerformence();
				return E_ReturnState::SUCCESS;
			}

			delete runtime;
			sleep(1);
		}	
	}

	E_ReturnState RunSolutionOnce()
	{
		runtime = new RuntimeCtrlOcl(false);
		INFO("initialize device.");						InitDev();
		INFO("generate source, compiler, worksize.");	GenerateSolution();
		INFO("compiler kernel and program.");			SetupSolution();
		INFO("set arg and launch kernel.");				LaunchSolution(false);
		INFO("collect performence.");					GetPerformence();
		INFO("copy result back to cpu.");				GetBackResult();
		INFO("release device.");						ReleaseDev();
		delete runtime;
	}

	virtual E_ReturnState LaunchSolution(bool isWarmup)
	{
		std::list<T_KernelArgu>::iterator args;

		INFO("setup arguments.");
		int i = 0;
		for (args = SolutionConfig->KernelArgus->begin();
			args != SolutionConfig->KernelArgus->end(); args++)
		{
			if ((*args).isVal == true)
			{
				if ((*args).ptr == NULL)
				{
					DevCheckFunc(clSetKernelArg(runtime->kernel, i, sizeof(cl_mem), (void*)NULL));
				}
				else
				{
					DevCheckFunc(clSetKernelArg(runtime->kernel, i, (*args).size, (void*)((*args).ptr)));
				}
			}
			else
			{
				DevCheckFunc(clSetKernelArg(runtime->kernel, i, (*args).size, &(*args).ptr));
			}
			i++;
		}

		INFO("launch kernel.");
		if (isWarmup)
		{
			runtime->LanchKernel();
			usleep(1);
		}
		else
		{
			for (int i = 0; i < RepeatTime; i++)
			{
				runtime->LanchKernel();
				ElapsedTimes.push_back(runtime->ElapsedTime);
				usleep(1);
			}
		}

		return E_ReturnState::SUCCESS;
	}

	virtual E_ReturnState SetupSolution()
	{
		runtime->KernelName = SolutionConfig->KernelName;
		runtime->KernelSrcType = SolutionConfig->KernelSrcType;
		runtime->extCompilerOpt = SolutionConfig->extCompilerOpt;
		SolutionConfig->b_wk0 = SolutionConfig->g_wk0 / SolutionConfig->l_wk0;
		SolutionConfig->b_wk1 = SolutionConfig->g_wk1 / SolutionConfig->l_wk1;
		SolutionConfig->b_wk2 = SolutionConfig->g_wk2 / SolutionConfig->l_wk2;
		runtime->SetBlockSize(dim3(SolutionConfig->l_wk0, SolutionConfig->l_wk1, SolutionConfig->l_wk2));
		runtime->SetGridSize(dim3(SolutionConfig->b_wk0, SolutionConfig->b_wk1, SolutionConfig->b_wk2));
		
		printf("l_wk=(%d, %d, %d)\n", SolutionConfig->l_wk0, SolutionConfig->l_wk1, SolutionConfig->l_wk2);
		printf("b_wk=(%d, %d, %d)\n", SolutionConfig->b_wk0, SolutionConfig->b_wk1, SolutionConfig->b_wk2);
		printf("g_wk=(%d, %d, %d)\n", SolutionConfig->g_wk0, SolutionConfig->g_wk1, SolutionConfig->g_wk2);
		std::cout << "compile options=" << SolutionConfig->extCompilerOpt << std::endl;

		// build source file
		runtime->GetFilesName(SolutionConfig->KernelFile);
		runtime->KernelString = SolutionConfig->KernelString;
		runtime->CreatSolution();

		// warm up
		INFO("warm up.");
		LaunchSolution(true);

		ElapsedTimes.clear();

		return E_ReturnState::SUCCESS;
	}

	E_ReturnState GetPerformence()
	{
		// ƽ��ʱ��
		std::list<double>::iterator elp;
		AverageScore.ElapsedTime = 0;
		for (elp = ElapsedTimes.begin(); elp != ElapsedTimes.end(); elp++)
		{
			//printf("elapsed time %.3f us\n",*elp * 1e6);
			AverageScore.ElapsedTime += *elp;
		}
		AverageScore.ElapsedTime /= ElapsedTimes.size();

		// ���ʱ��
		ElapsedTimes.sort();
		BestScore.ElapsedTime = ElapsedTimes.front();

		// ���ܻ���
		AverageScore.Flops = ProblemConfig->Calculation / AverageScore.ElapsedTime;
		AverageScore.Performence = ProblemConfig->TheoryElapsedTime / AverageScore.ElapsedTime;
		BestScore.Flops = ProblemConfig->Calculation / BestScore.ElapsedTime;
		BestScore.Performence = ProblemConfig->TheoryElapsedTime / BestScore.ElapsedTime;

		printf("best elapsed time: %.3f (us).\n", BestScore.ElapsedTime * 1e6);
		printf("best performence: %.1f (Gflops) = %.1f%%.\n", BestScore.Flops * 1e-9, BestScore.Performence * 100);
		printf("average elapsed time: %.3f (us).\n", AverageScore.ElapsedTime * 1e6);
		printf("average performence: %.1f (Gflops) = %.1f%%.\n", AverageScore.Flops * 1e-9, AverageScore.Performence * 100);
		
		if ((ProblemBestTime < 0) || (ProblemBestTime > AverageScore.ElapsedTime))
		{
			ProblemBestTime = AverageScore.ElapsedTime;
			ProblemBestPerformence = AverageScore.Performence;
			SolutionConfig->KernelSearchSpace.RecordBestComb();
		}
	}

	void printIndex(int *index, char* name)
	{
		int groupNum = SolutionConfig->g_wk0 / SolutionConfig->l_wk0;
		int grpNumPerCU = (groupNum + CU_NUM - 1) / CU_NUM;
		int waveNumPerCU = grpNumPerCU * (SolutionConfig->l_wk0 / WAVE_SIZE);
		int simuGrpIdx = 0;
		int grpIdxBase;

		printf("\t|---------------------------------------------------------\n");
		printf("\t| index name = %s\n", name);
		printf("\t| group size = %d\n", SolutionConfig->l_wk0);
		printf("\t| group number = %d\n", groupNum);
		printf("\t| group number per cu = %d\n", grpNumPerCU);
		printf("\t| wave number per cu = %d\n", waveNumPerCU);
		printf("\t|---------------------------------------------------------\n");
		for (int se = 0; se < SE_NUM; se++)
		{
			printf("SE=%d:", se);
			grpIdxBase = se;

			for (int cu = 0; cu < CU_PER_SE; cu++)
			{
				printf("\t[%02d]: ", cu);
				simuGrpIdx = grpIdxBase;

				while (simuGrpIdx < groupNum)
				{
					printf("%03d, ", index[simuGrpIdx]);
					simuGrpIdx += CU_NUM;
				}
				printf("\n");
				grpIdxBase += 4;
			}
			printf("\n");
		}
	}
	
	virtual E_ReturnState InitDev() = 0;
	virtual E_ReturnState GenerateSolutionConfigs() = 0;
	virtual E_ReturnState GenerateSolution() = 0;
	virtual E_ReturnState GetBackResult() = 0;
	virtual void ReleaseDev() = 0;
	virtual void ReportProblemPerformence()
	{
		printf("please report best perfomence.\n");
	}

	T_ProblemConfig * ProblemConfig;					// ��ǰ���ڴ������������
	T_SolutionConfig * SolutionConfig;					// ��ǰ���ڴ���Ľ����������
	std::list<T_SolutionConfig*> *SolutionConfigList;	// ���н����������

	int RepeatTime;
	RuntimeCtrl * runtime;			
	std::list<double> ElapsedTimes;		// ÿ�����еĺ�ʱ
	T_Score BestScore;					// ��ǰ����������õ��������
	T_Score AverageScore;				// ��ǰ����������õ�ƽ������
	double ProblemBestTime;				// ��ǰ�������õ��������ʱ��
	double ProblemBestPerformence;		// ��ǰ�������õ��������
};

/************************************************************************/
/* ������																*/
/************************************************************************/
class ProblemCtrlBase
{
public:
	ProblemCtrlBase()
	{
		ProblemConfigList = new std::list<T_ProblemConfig*>;
	}

	void RunProblem()
	{
		printf("************************************************************************\n");
		printf("* Problem Name: %s.\n", ProblemName.c_str());
		printf("************************************************************************\n");

		// ======================================================================
		// ��������ռ�
		// ======================================================================
		INFO("generate problem config list.");
		ProblemConfigList->clear();
		GenerateProblemConfigs();

		// ======================================================================
		// ����problem�����ռ�,���������ռ�
		// ======================================================================
		std::list<T_ProblemConfig*>::iterator problemCfgIt;
		for (problemCfgIt = ProblemConfigList->begin(); 
			problemCfgIt != ProblemConfigList->end(); 
			problemCfgIt++)
		{
			ProblemConfig = *problemCfgIt;

			printf("************************************************************************\n");
			printf("* Problem Name: %s.\n", ProblemName.c_str());
			printf("* Problem Config: %s.\n", ProblemConfig->ConfigName.c_str());
			printf("************************************************************************\n");

			if (ProblemConfig->ProblemParamSpace.ParamNum > 0)
			{
				RunOneProblemConfig();
			}
			else
			{
				RunProblemOnce();
			}
		}
	}

	E_ReturnState RunOneProblemConfig()
	{
		while (true)
		{
			Solution->ProblemBestTime = -1;
			INFO("initialize host.");			InitHost();
			INFO("run host calculate.");		Host();
			INFO("solve this problem.");		Solution->RunSolution(ProblemConfig);
			INFO("verify device calculation.");	Verify();
			INFO("release host.");				ReleaseHost();

			INFO("search problem parameters.");
			if (ProblemConfig->ProblemParamSpace.GetNexComb() == E_ReturnState::FAIL)
			{
				INFO("search problem parameters finished.");
				break;
			}
		}
	}

	E_ReturnState RunProblemOnce()
	{
		Solution->ProblemBestTime = -1;
		INFO("initialize host.");				InitHost();
		INFO("run host calculate.");			Host();
		INFO("solve this problem.");			Solution->RunSolution(ProblemConfig);
		INFO("verify device calculation.");		Verify();
		INFO("release host.");					ReleaseHost();

		return E_ReturnState::SUCCESS;
	}

	virtual E_ReturnState GenerateProblemConfigs() = 0;
	virtual E_ReturnState InitHost() = 0;
	virtual E_ReturnState Host() = 0;
	virtual E_ReturnState Verify() = 0;
	virtual void ReleaseHost() = 0;

	std::string ProblemName;
	std::list<T_ProblemConfig*> *ProblemConfigList;	// ������������
	T_ProblemConfig *ProblemConfig;					// ��ǰ���ڴ������������
	SolutionCtrlBase * Solution;
};

#include "TestProblem.h"

#include "IsaFlat.h"
#include "IsaGlobal.h"
#include "IsaDs.h"
#include "IsaMubuf.h"
#include "IsaSleep.h"

#include "VectorAdd.h"
#include "ProducerConsumer.h"

#include "ConvFwd1x1.h"

