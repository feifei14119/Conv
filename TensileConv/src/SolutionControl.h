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

	size_t l_wk0, l_wk1, l_wk2;
	size_t g_wk0, g_wk1, g_wk2;
	size_t b_wk0, b_wk1, b_wk2;
}T_SolutionConfig;

struct T_ProblemConfig;
/************************************************************************/
/* solution ���� (so called generic solver)			                    */
/************************************************************************/
class SolutionCtrlBase
{
public:
	// �˴����԰�������չ���ô�����
	SolutionCtrlBase()
	{
		RepeatTime = 1;
		ProblemBestTime = -1;
		cmdArgs = CmdArgs::GetCmdArgs();
		pOclRt = RuntimeOCL::GetInstance();
		stream = pOclRt->CreatCmdQueue(true);
		SolutionConfigList = new std::list<T_SolutionConfig*>;
		SolutionConfigList->clear();
	}
	~SolutionCtrlBase() { delete stream; }


public:
	void RunAllSolution();
	E_ReturnState RunOneSolution();

	void CleanSolution();

	virtual E_ReturnState LaunchSolution(bool isWarmup);
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
	//RuntimeCtrl * runtime;			
	std::list<double> ElapsedTimes;		// ÿ�����еĺ�ʱ
	T_Score BestScore;					// ��ǰ����������õ��������
	T_Score AverageScore;				// ��ǰ����������õ�ƽ������
	double ProblemBestTime;				// ��ǰ�������õ��������ʱ��
	double ProblemBestPerformence;		// ��ǰ�������õ��������

protected:
	CmdArgs * cmdArgs;
	int devId;

	RuntimeOCL * pOclRt;
	CmdQueueOCL* stream;	// one command queue for whole solution configs
};
