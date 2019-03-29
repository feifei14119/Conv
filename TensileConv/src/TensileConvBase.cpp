
#include "TensileConvBase.h"

using namespace TensileConv;
using namespace AutoTune;

/************************************************************************/
/* solution����															*/
/************************************************************************/
void SolutionCtrlBase::RunSolution()
{
	time_t t1 = time(0);

	PrintSeperator('=');
	INFO("* solution Name: %s.", solutionName.c_str());
	PrintSeperator('=');

	// ���ɽ�������ռ�
	generateSolutionParamSpace();
	searchSpace->InitSearching();

	// ����ÿ��problem��solution�����ռ�
#define TempDo(x)	if(x != E_ReturnState::SUCCESS) goto CONTINUE;
	while (1)
	{
		TempDo(getKernelParam());
		TempDo(generateKernel());
		TempDo(prepareKernelArgs());
		TempDo(launchKernel());

		CONTINUE:
		releaseDevMem();
		if (searchSpace->GenerateNextComb() != E_ReturnState::SUCCESS)
		{
			LOG("search solution parameters finished.");
			break;
		}

		sleep(0.5);
		PrintSeperator('-');
	}
#undef TempDo(x)

	time_t t2 = time(0);
	searchElapsedSec = difftime(t2,t1);
}

E_ReturnState SolutionCtrlBase::launchKernel()
{
	LOG("warmup.");
	{
		stream->Launch(kernel, global_sz, group_sz, &profEvt);
		stream->Finish();
		usleep(0.1);
	}

	int loopCnt = 0;
	double t = 0, elapsedTime = 0;
	for (int i = 0; i < repeatTime; i++)
	{
		stream->Launch(kernel, global_sz, group_sz, &profEvt);
		stream->Flush();
		stream->Finish();
		usleep(0.01);

		t = rtOcl->GetProfilingTime(&profEvt);
		elapsedTime += t;

		loopCnt++;
		if (t > 5e-3)
		{
			break;
		}
	}

	LOG("launch kernel %d times.", loopCnt);
	elapsedTime /= loopCnt;
	recordScore(elapsedTime);
	
	return E_ReturnState::SUCCESS;
}

void SolutionCtrlBase::recordScore(double elapsedTime)
{
	searchSpace->SetOneCombScore(elapsedTime);
	if (solutionScore.ElapsedTime > elapsedTime)
	{
		searchSpace->RecordCurrComb();
	}

	T_Score score;
	score.ElapsedTime = elapsedTime;
	score.Flops = problem->Calculation() / score.ElapsedTime;
	score.Performence = problem->TheoryElapsedTime() / score.ElapsedTime;
	LOG("elapsed = %.1f(us), performence = %.1f(Gflops) = %.1f%%",
		score.ElapsedTime * 1e6, score.Flops * 1e-9, score.Performence * 100);

	if (solutionScore.ElapsedTime > score.ElapsedTime)
	{
		solutionScore.ElapsedTime = score.ElapsedTime;
		solutionScore.Performence = score.Performence;
		solutionScore.Flops = problem->Calculation() / solutionScore.ElapsedTime;
	}
	LOG("Best for now: elapsed = %.1f(us), performence = %.1f%%",
		solutionScore.ElapsedTime * 1e6, solutionScore.Performence * 100);
}

/************************************************************************/
/* solver ����															*/
/************************************************************************/
void SolverCtrlBase::RunSolver()
{
	// add solutions to solver
	generateSolver();

	// ����solver�ĸ���solution
	bestSolution = (*solutionList)[0];
	for(SolutionCtrlBase * solution : *solutionList)
	{
		if (EnSearch)	solution->RunSolution();
		
		T_Score score = solution->SolutionScore();
		scoreList->push_back(score);

		if (bestScore.ElapsedTime > score.ElapsedTime)
		{
			bestScore = score;
			bestSolution = solution;
		}
	}

	// best solution
	bestSolution->GetBestKernel();
}

/************************************************************************/
/* problem ����															*/
/************************************************************************/
void ProblemCtrlBase::RunProblem()
{
	PrintSeperator('*');
	INFO("* Problem Name: %s.", problemName.c_str());
	PrintSeperator('*');

	// ����problem�����ռ�,���������ռ�
	while (true)
	{
		searchSpace->InitSearching();
		initHostParam();
		if (EnSearch)	caculateTheoryPerformance();
		if (EnSearch)	runHostCompute();
		solver->RunSolver();
		if (EnSearch)	verifyDevCompute();
		if (EnSearch)	releaseHostParam();

		if (searchSpace->GenerateNextComb() != E_ReturnState::SUCCESS)
		{
			LOG("search problem parameters finished.");
			break;
		}

		sleep(1);
	}
}

