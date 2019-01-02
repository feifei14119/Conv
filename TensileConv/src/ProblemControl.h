#pragma once

#include <stdarg.h>
#include <vector>
#include <list>
#include "../common/ff_utils.h"

#include "SolutionControl.h"
#include "AutoTuning.h"

#include "unistd.h"

using namespace AutoTune;

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

		problemConfigList = new std::list<T_ProblemConfig*>;
		problemConfigList->clear();
	}
	ProblemCtrlBase(std::string name)
	{
		problemName = name;

		cmdArgs = CmdArgs::GetCmdArgs();
		rtOcl = RuntimeOCL::GetInstance();

		problemConfigList = new std::list<T_ProblemConfig*>;
		problemConfigList->clear();
	}

	void RunAllProblem();

protected:
	CmdArgs * cmdArgs;
	RuntimeOCL * rtOcl;
	SolutionCtrlBase * solution;

	std::string problemName;
	std::list<T_ProblemConfig*> *problemConfigList;	// ������������
	T_ProblemConfig *problemConfig;					// ��ǰ���ڴ������������

	E_ReturnState runOneProblem();
	virtual E_ReturnState initHostParam() = 0;
	virtual E_ReturnState runHostCompute() = 0;
	virtual E_ReturnState verifyDevCompute() = 0;
	virtual void releaseHostParam() = 0;
	virtual void caculateTheoryPerformance() {}
};

