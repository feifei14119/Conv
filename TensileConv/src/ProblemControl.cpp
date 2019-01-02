
#include "ProblemControl.h"

using namespace AutoTune;

/************************************************************************/
/* ������																*/
/************************************************************************/
void ProblemCtrlBase::RunAllProblem()
{
	PRINT_SEPARATOR1();
	INFO(" Problem Name: %s.", problemName.c_str());
	PRINT_SEPARATOR1();

	// ======================================================================
	// ����problem�����ռ�,���������ռ�
	// ======================================================================
	std::list<T_ProblemConfig*>::iterator problemCfgIt;
	for (problemCfgIt = problemConfigList->begin(); problemCfgIt != problemConfigList->end(); problemCfgIt++)
	{
		problemConfig = *problemCfgIt;
		runOneProblem();
	}
}

E_ReturnState ProblemCtrlBase::runOneProblem()
{
	while (true)
	{
		INFO("initialize host.");				initHostParam(); caculateTheoryPerformance();
		INFO("run host calculate.");			runHostCompute();
		INFO("solve this problem.");			solution->RunAllSolution(problemConfig);
		INFO("verify device calculation.");		verifyDevCompute();
		INFO("release host.");					releaseHostParam();

		if (problemConfig->ProblemParamSpace.ParamNum > 0)
		{
			INFO("search problem parameters.");
			if (problemConfig->ProblemParamSpace.GetNexComb() == E_ReturnState::FAIL)
			{
				INFO("search problem parameters finished.");
				break;
			}
		}
		else
		{
			break;
		}
	}

	return E_ReturnState::SUCCESS;
}
#pragma endregion
