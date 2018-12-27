#pragma once

#include <stdarg.h>
#include <vector>
#include <list>

#include "../common/ff_utils.h"
#include "AutoTuning.h"

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
	SolutionConfigTpye() {}
	SolutionConfigTpye(std::string name) { ConfigName = name; }

	std::string ConfigName;				// �����������
	SearchSpace KernelSearchSpace;		// ����������������ռ�
	void * extConfig;

	std::string KernelName;			// kernel function name, will used to find source file
	std::string KernelDir;
	std::string KernelFile;			// ����ָ���ļ�������ʹ��KernelName�Ƶ�.��Ҫ��׺
	std::string KernelFileFullName;
	std::string KernelString;
	E_ProgramType KernelSrcType;
	std::string extCompilerOpt;

	dim3 group_sz;
	dim3 group_num;
	dim3 global_sz;
}T_SolutionConfig;

/************************************************************************/
/* problem ����				                                            */
/************************************************************************/
typedef struct ProblemConfigType
{
	ProblemConfigType(std::string name) { ConfigName = name; }
	ProblemConfigType() {}

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
	// �˴����԰�������չ���ô�����
	SolutionCtrlBase()
	{
		RepeatTime = 100;
		ProblemBestTime = -1;
		cmdArgs = CmdArgs::GetCmdArgs();
		rtOcl = RuntimeOCL::GetInstance();
		stream = rtOcl->CreatCmdQueue(true);
		SolutionConfigList = new std::list<T_SolutionConfig*>;
		SolutionConfigList->clear();
	}
	~SolutionCtrlBase() { delete stream; delete SolutionConfigList; }


public:
	void RunAllSolution(T_ProblemConfig *problem);
	E_ReturnState RunOneSolution();
	virtual E_ReturnState LaunchSolution(bool isWarmup);
	E_ReturnState GetPerformence();	
	
	virtual E_ReturnState InitDev() = 0;
	virtual E_ReturnState GenerateSolutionConfigs() = 0;
	virtual E_ReturnState GenerateSolution() = 0;
	virtual E_ReturnState GetBackResult() = 0;
	virtual void ReleaseDev() = 0;
	virtual void ReportProblemPerformence()
	{
		printf("please report best perfomence.\n");
	}


	int RepeatTime;	
	T_Score BestScore;					// ��ǰ����������õ��������
	T_Score AverageScore;				// ��ǰ����������õ�ƽ������
	double ProblemBestTime;				// ��ǰ�������õ��������ʱ��
	double ProblemBestPerformence;		// ��ǰ�������õ��������

protected:
	CmdArgs * cmdArgs;
	RuntimeOCL * rtOcl;
	CmdQueueOCL* stream;	// one command queue for whole solution configs
	KernelOCL * kernel;
	cl_event profEvt;

	std::list<T_SolutionConfig*> *SolutionConfigList;	// ���н����������
	T_ProblemConfig * ProblemConfig;					// ��ǰ���ڴ������������
	T_SolutionConfig * SolutionConfig;					// ��ǰ���ڴ���Ľ����������
	
	std::vector<double> elapsedTimes;		// ������������ظ�ִ�е�ʱ��

};

namespace feifei
{
	extern int next2pow(int n);
	extern int log2(int value);
	extern int divCeil(int a, int b);
	extern void printIndex(int *index, char* name, dim3 g_wk, dim3 l_wk);
}
