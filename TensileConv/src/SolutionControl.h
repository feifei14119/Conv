#pragma once

#include <limits>
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
	double ElapsedTime;	//(s)
	double Flops;		//(Flops)
	double Performence;	//(%)
}T_Score;

/************************************************************************/
/* solution ����				                                            */
/* ������Ҫ���ݸ� KernelWriterBase ��ͨ�ò���								*/
/************************************************************************/
typedef struct SolutionConfigTpye
{
	SolutionConfigTpye() {}
	SolutionConfigTpye(std::string name) { ConfigName = name; }

	std::string ConfigName;				// ��������
	SearchSpace KernelSearchSpace;		// ����������������ռ�
	void * extConfig;

	std::string KernelName;			// kernel function name, will used to find source file
	std::string KernelDir;
	std::string KernelFile;			// ����ָ���ļ�������ʹ��KernelName�Ƶ�.��Ҫ��׺
	std::string KernelFileFullName;
	std::string kernelString;
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
	ProblemConfigType() {}
	ProblemConfigType(std::string name) { ConfigName = name; }

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
		repeatTime = 1;
		solutionScore.ElapsedTime = (std::numeric_limits<double>::max)();
		solutionScore.Performence = 0;

		cmdArgs = CmdArgs::GetCmdArgs();
		rtOcl = RuntimeOCL::GetInstance();
		stream = rtOcl->CreatCmdQueue(true);

		solutionConfigList = new std::list<T_SolutionConfig*>;
		solutionConfigList->clear();
	}
	~SolutionCtrlBase() { delete stream; delete solutionConfigList; }
	
	void RunAllSolution(T_ProblemConfig *problem);

protected:
	CmdArgs * cmdArgs;
	RuntimeOCL * rtOcl;
	CmdQueueOCL* stream;	// one command queue for all solution configs
	KernelOCL * kernel;
	cl_event profEvt;

	std::list<T_SolutionConfig*> *solutionConfigList;	// ���н����������
	T_ProblemConfig * problemConfig;					// ��ǰ���ڴ������������
	T_SolutionConfig * solutionConfig;					// ��ǰ���ڴ���Ľ����������

	int repeatTime;
	T_Score configScore;				// ��ǰ���õ�ƽ������
	T_Score solutionScore;				// ȫ�����õ�ƽ������

	virtual E_ReturnState generateSolutionConfigs() = 0;
	E_ReturnState runOneSolution();
	virtual E_ReturnState generateKernel() = 0;
	virtual E_ReturnState generateKernelParam() = 0;
	virtual E_ReturnState launchKernel();
	virtual E_ReturnState getBackResult() = 0;
	virtual void releaseKernelParam() = 0;
	virtual void reportProblemPerformence(){}
};

namespace feifei
{
	extern int next2pow(int n);
	extern int log2(int value);
	extern int divCeil(int a, int b);
	extern void printIndex(int *index, char* name, dim3 g_wk, dim3 l_wk);
}
