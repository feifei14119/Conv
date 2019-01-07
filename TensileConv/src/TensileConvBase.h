#pragma once

#include <limits>
#include <stdarg.h>
#include <vector>
#include <list>

#include "../common/ff_utils.h"
#include "AutoTuning.h"

#include "unistd.h"

namespace TensileConv {

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
/* solution ���� (so called generic solver)			                    */
/************************************************************************/
class ProblemCtrlBase;
class SolutionCtrlBase
{
public:
	SolutionCtrlBase(ProblemCtrlBase * problem)
	{
		repeatTime = 100;
		solutionScore.ElapsedTime = (std::numeric_limits<double>::max)();
		solutionScore.Performence = 0;

		cmdArgs = CmdArgs::GetCmdArgs();
		rtOcl = RuntimeOCL::GetInstance();
		stream = rtOcl->CreatCmdQueue(true);

		this->problem = problem;
		solutionParamSpace = new AutoTune::SearchSpace();
	}
	virtual ~SolutionCtrlBase() { delete stream; delete solutionParamSpace; }

	void RunSolution();

	std::string KernelName() { return kernelName; }
	std::string KernelFile() { return kernelFile; }
	dim3 GroupSize() { return group_sz; }
	dim3 GlobalSize() { return global_sz; }
	T_Score SolutionScore() { return solutionScore; }

protected:
	CmdArgs * cmdArgs;
	RuntimeOCL * rtOcl;
	CmdQueueOCL* stream;	// one command queue for all solution configs
	KernelOCL * kernel;
	cl_event profEvt;

	ProblemCtrlBase * problem;
	std::string solutionName;						// ��������
	AutoTune::SearchSpace *solutionParamSpace;		// ����������������ռ�

	std::string kernelName;
	std::string kernelFile;
	dim3 group_sz;
	dim3 global_sz;

	int repeatTime;
	T_Score configScore;				// ��ǰ���õ�ƽ������
	T_Score solutionScore;				// ȫ�����õ�ƽ������

	virtual E_ReturnState generateSolutionParamSpace() = 0;
	virtual E_ReturnState getKernelParam() {}
	virtual E_ReturnState generateKernel() = 0;
	virtual E_ReturnState prepareKernelArgs() = 0;
	virtual E_ReturnState launchKernel();
	virtual void getBackResult() = 0;
	virtual void releaseDevMem() = 0;
	virtual void getBestKernel() {}

	// ��ӡ�±�
	void printIndex(int *index, char* name, dim3 g_wk, dim3 l_wk);
};

/************************************************************************/
/* ������																*/
/************************************************************************/
class ProblemCtrlBase
{
public:
	ProblemCtrlBase()
	{
		cmdArgs = CmdArgs::GetCmdArgs();
		rtOcl = RuntimeOCL::GetInstance();

		problemParamSpace = new AutoTune::SearchSpace();
	}
	ProblemCtrlBase(std::string name)
	{
		problemName = name;

		cmdArgs = CmdArgs::GetCmdArgs();
		rtOcl = RuntimeOCL::GetInstance();

		problemParamSpace = new AutoTune::SearchSpace();
	}
	virtual ~ProblemCtrlBase() { delete problemParamSpace; }

	void RunAllProblem();
	SolutionCtrlBase * Solution() { return solution; }
	double Calculation() { return calculation; }
	double TheoryElapsedTime() { return theoryElapsedTime; }

	// TODO: dump/load input/output data

protected:
	CmdArgs * cmdArgs;
	RuntimeOCL * rtOcl;
	SolutionCtrlBase * solution;

	std::string problemName;
	AutoTune::SearchSpace *problemParamSpace;		// ������������ռ�

	double calculation;					// ��ǰ���ڴ�����������õļ�����
	double theoryElapsedTime;			// ��ǰ���ڴ�����������õ�����ִ��ʱ��

	virtual E_ReturnState initHostParam() = 0;
	virtual E_ReturnState runHostCompute() = 0;
	virtual E_ReturnState verifyDevCompute() = 0;
	virtual void releaseHostParam() = 0;
	virtual void caculateTheoryPerformance() {}
};
}

