#pragma once

#include "BasicClass.h"
#include "RuntimeControl.h"
#include "ProblemControl.h"

/************************************************************************/
/* ��չ����                                                              */
/************************************************************************/
typedef struct ExtVectAddSolutionConfigTpye
{
#define FIX_WORKGROUP_SIZE (64)
}T_ExtVectAddSolutionConfig;

typedef struct ExtVectAddProblemConfigType
{
#define VECTOR_SIZE (1024)
	size_t vectorSize;
	float *h_a, *h_b, *h_c, *c_ref;
}T_ExtVectAddProblemConfig;

/************************************************************************/
/* solution����                                                          */
/************************************************************************/
class VectorAddSolution : public SolutionCtrlBase
{
private:
	T_KernelArgu d_a, d_b, d_c;
public:
	/************************************************************************/
	/* �����Դ�                                                            */
	/************************************************************************/
	E_ReturnState InitDev()
	{
		T_ExtVectAddProblemConfig * exCfg = (T_ExtVectAddProblemConfig *)ProblemConfig->extConfig;

		DevMalloc((void**)&(d_a.ptr), exCfg->vectorSize * sizeof(float));
		DevMalloc((void**)&(d_b.ptr), exCfg->vectorSize * sizeof(float));
		DevMalloc((void**)&(d_c.ptr), exCfg->vectorSize * sizeof(float));

		SolutionConfig->KernelArgus = new std::list<T_KernelArgu>;
		d_a.size = sizeof(cl_mem);	d_a.isVal = false;	SolutionConfig->KernelArgus->push_back(d_a);
		d_b.size = sizeof(cl_mem);	d_b.isVal = false;	SolutionConfig->KernelArgus->push_back(d_b);
		d_c.size = sizeof(cl_mem);	d_c.isVal = false;	SolutionConfig->KernelArgus->push_back(d_c);

		Copy2Dev((cl_mem)(d_a.ptr), exCfg->h_a, exCfg->vectorSize * sizeof(float));
		Copy2Dev((cl_mem)(d_b.ptr), exCfg->h_b, exCfg->vectorSize * sizeof(float));

		return E_ReturnState::SUCCESS;
	}

	/************************************************************************/
	/* ���ؽ��                                                            */
	/************************************************************************/
	E_ReturnState GetBackResult()
	{
		T_ExtVectAddProblemConfig * exCfg = (T_ExtVectAddProblemConfig *)ProblemConfig->extConfig;
		Copy2Hst(exCfg->h_c, (cl_mem)(d_c.ptr), exCfg->vectorSize * sizeof(float));
	}

	/************************************************************************/
	/* �ͷ��Դ�	                                                           */
	/************************************************************************/
	void ReleaseDev()
	{
		DevFree((cl_mem)(d_a.ptr));
		DevFree((cl_mem)(d_b.ptr));
		DevFree((cl_mem)(d_c.ptr));
	}

	/************************************************************************/
	/* ����problem������solution�����ռ�                                      */
	/************************************************************************/
	E_ReturnState GenerateSolutionConfigs()
	{
		T_SolutionConfig * solutionConfig;
		T_ExtVectAddSolutionConfig * extSolutionConfig;

		// ======================================================================
		// solution config 1: OCL
		// ======================================================================
		extSolutionConfig = new T_ExtVectAddSolutionConfig();

		solutionConfig = new T_SolutionConfig();
		solutionConfig->ConfigName = "OclSolution";
		solutionConfig->extConfig = extSolutionConfig;

		// ----------------------------------------------------------------------
		SolutionConfigList->push_back(solutionConfig);

		// ======================================================================
		// solution config 2: ASM
		// ======================================================================
		extSolutionConfig = new T_ExtVectAddSolutionConfig();

		solutionConfig = new T_SolutionConfig();
		solutionConfig->ConfigName = "AsmSolution";
		solutionConfig->extConfig = extSolutionConfig;

		// ----------------------------------------------------------------------
		SolutionConfigList->push_back(solutionConfig);

		return E_ReturnState::SUCCESS;
	}

	/************************************************************************/
	/* ����solution��������source, complier��worksize                         */
	/************************************************************************/
	E_ReturnState GenerateSolution()
	{
		T_ExtVectAddProblemConfig * extProblem = (T_ExtVectAddProblemConfig *)ProblemConfig->extConfig;
		T_ExtVectAddSolutionConfig * extSolution = (T_ExtVectAddSolutionConfig *)SolutionConfig->extConfig;
				
		// ======================================================================
		// ���ɴ���
		// ======================================================================
		if (SolutionConfig->ConfigName == "OclSolution")
		{
			SolutionConfig->KernelName = "VectorAdd";
			SolutionConfig->KernelSrcType = E_KernleType::KERNEL_TYPE_OCL_FILE;
		}
		else if (SolutionConfig->ConfigName == "AsmSolution")
		{
			SolutionConfig->KernelName = "VectorAdd";
			SolutionConfig->KernelSrcType = E_KernleType::KERNEL_TYPE_GAS_FILE;
		}

		// ======================================================================
		// ����worksize
		// ======================================================================
		SolutionConfig->l_wk0 = FIX_WORKGROUP_SIZE;
		SolutionConfig->l_wk1 = 1;
		SolutionConfig->l_wk2 = 1;
		SolutionConfig->g_wk0 = extProblem->vectorSize;
		SolutionConfig->g_wk1 = 1;
		SolutionConfig->g_wk2 = 1;

		return E_ReturnState::SUCCESS;
	}
};

/************************************************************************/
/* �������                                                             */
/************************************************************************/
class VectorAddProblem : public ProblemCtrlBase
{
public:
	VectorAddProblem()
	{
		ProblemName = "VectorAdd";
		Solution = new VectorAddSolution();
		ProblemConfigList = new std::list<T_ProblemConfig*>;
	}

public:
	/************************************************************************/
	/* ��������ռ�													        */
	/************************************************************************/
	E_ReturnState GenerateProblemConfigs()
	{
		T_ProblemConfig * problemConfig;
		T_ExtVectAddProblemConfig * extProblemConfig;

		// ----------------------------------------------------------------------
		// problem config 1
		extProblemConfig = new T_ExtVectAddProblemConfig();
		extProblemConfig->vectorSize = VECTOR_SIZE;

		problemConfig = new T_ProblemConfig();
		problemConfig->ConfigName = "512";
		problemConfig->extConfig = extProblemConfig;

		ProblemConfigList->push_back(problemConfig);
	}

	/************************************************************************/
	/* ������ʼ��                                                            */
	/************************************************************************/
	E_ReturnState InitHost()
	{
		T_ExtVectAddProblemConfig * exCfg = (T_ExtVectAddProblemConfig *)ProblemConfig->extConfig;

		ProblemConfig->Calculation = exCfg->vectorSize;
		ProblemConfig->TheoryElapsedTime = ProblemConfig->Calculation / RuntimeCtrlBase::DeviceInfo.Fp32Flops;
		printf("Calculation = %.3f G\n", ProblemConfig->Calculation * 1e-9);
		printf("TheoryElapsedTime = %.3f us \n", ProblemConfig->TheoryElapsedTime * 1e6);

		exCfg->h_a = (float*)HstMalloc(exCfg->vectorSize * sizeof(float));
		exCfg->h_b = (float*)HstMalloc(exCfg->vectorSize * sizeof(float));
		exCfg->h_c = (float*)HstMalloc(exCfg->vectorSize * sizeof(float));
		exCfg->c_ref = (float*)HstMalloc(exCfg->vectorSize * sizeof(float));
		
		for (int i = 0; i < exCfg->vectorSize; i++)
		{
			exCfg->h_a[i] = i;
			exCfg->h_b[i] = 2;
			exCfg->h_c[i] = 0;
		}

		return E_ReturnState::SUCCESS;
	}

	/************************************************************************/
	/* HOST��                                                               */
	/************************************************************************/
	E_ReturnState Host()
	{
		T_ExtVectAddProblemConfig * exCfg = (T_ExtVectAddProblemConfig *)ProblemConfig->extConfig;

		for (int i = 0; i < exCfg->vectorSize; i++)
		{
			exCfg->c_ref[i] = exCfg->h_a[i] + exCfg->h_b[i];
		}
		return E_ReturnState::SUCCESS;
	}

	/************************************************************************/
	/* У��                                                                 */
	/************************************************************************/
	E_ReturnState Verify()
	{
		T_ExtVectAddProblemConfig * exCfg = (T_ExtVectAddProblemConfig *)ProblemConfig->extConfig;
		
		float diff = 0;
		for (int i = 0; i < exCfg->vectorSize; i++)
		{
			diff += (exCfg->c_ref[i] - exCfg->h_c[i]) * (exCfg->c_ref[i] - exCfg->h_c[i]);
		}
		diff /= exCfg->vectorSize;

		printf("mean err = %.1f.\n", diff);
		if (diff > MIN_FP32_ERR)
		{
			printf("err = %.2f\n", diff);
			printf("verify failed!\n");
			return E_ReturnState::FAIL;
		}
		printf("verify success.\n");
		return E_ReturnState::SUCCESS;
	}

	/************************************************************************/
	/* �ͷ�                                                                  */
	/************************************************************************/
	void ReleaseHost()
	{
		T_ExtVectAddProblemConfig * exCfg = (T_ExtVectAddProblemConfig *)ProblemConfig->extConfig;

		HstFree(exCfg->h_a);
		HstFree(exCfg->h_b);
		HstFree(exCfg->h_c);
		HstFree(exCfg->c_ref);
	}	
};
