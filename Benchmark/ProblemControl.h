#pragma once

#include <stdarg.h>
#include <vector>
#include "BasicClass.h"
#include "RuntimeControl.h"

#include "unistd.h"

using namespace AutoTune;

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

	SolutionConfigTpye(std::string name)
	{
		ConfigName = name;
	}
	SolutionConfigTpye()
	{
	}
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

	ProblemConfigType(std::string name)
	{
		ConfigName = name;
	}
	ProblemConfigType()
	{
	}
}T_ProblemConfig;

/************************************************************************/
/* solution ���� (so called generic solver)			                    */
/************************************************************************/
class SolutionCtrlBase
{
public:
	SolutionCtrlBase();

public:
	void RunSolution(T_ProblemConfig *problem);

	E_ReturnState RunOneSolutionConfig();

	E_ReturnState RunSolutionOnce();

	virtual E_ReturnState LaunchSolution(bool isWarmup);

	virtual E_ReturnState SetupSolution();

	E_ReturnState GetPerformence();

	void printIndex(int *index, char* name);
	
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
	ProblemCtrlBase();
	ProblemCtrlBase(std::string name);

public:
	void RunProblem();

	E_ReturnState RunOneProblemConfig();

	E_ReturnState RunProblemOnce();

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

