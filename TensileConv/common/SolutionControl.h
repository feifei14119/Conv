#pragma once

#include <stdarg.h>
#include <vector>
#include "ff_utils.h"
#include "AutoTuning.h"
#include "BackendEngine.h"

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
	std::string KernelDir;
	std::string KernelFile;			// ����ָ���ļ�������ʹ��KernelName�Ƶ�.��Ҫ��׺
	std::string KernelFileFullName;
	std::string KernelString;
	E_KernleType KernelSrcType;
	std::string extCompilerOpt;

	size_t l_wk0, l_wk1, l_wk2;
	size_t g_wk0, g_wk1, g_wk2;
	size_t b_wk0, b_wk1, b_wk2;

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
	SolutionCtrlBase()
	{
		RepeatTime = 1;
		SolutionConfigList = new std::list<T_SolutionConfig*>;

		cmdArgs = CmdArgs::GetCmdArgs();
		devId = *(int*)(cmdArgs->GetOneArg(E_ArgId::CMD_ARG_DEVICE));

		platform = (BackendEngineOCL*)(BackendEngine::Get("OpenCL"));
		gpuDevice = platform->GetDevice(devId);

		compiler = (OCLASMCompiler*)BackendEngine::GetCompiler(platform, "asm");
		bcompiler = (OCLBinaryCompiler*)BackendEngine::GetCompiler(platform, "bin");
	}

public:
	void RunSolution(T_ProblemConfig *problem);

	E_ReturnState RunOneSolutionConfig();

	E_ReturnState RunSolutionOnce();

	void CleanSolution();

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
	//RuntimeCtrl * runtime;			
	std::list<double> ElapsedTimes;		// ÿ�����еĺ�ʱ
	T_Score BestScore;					// ��ǰ����������õ��������
	T_Score AverageScore;				// ��ǰ����������õ�ƽ������
	double ProblemBestTime;				// ��ǰ�������õ��������ʱ��
	double ProblemBestPerformence;		// ��ǰ�������õ��������

protected:
	CmdArgs * cmdArgs;
	int devId;

	BackendEngineOCL * platform;		// Ӳ��ƽ̨���
	DeviceBase * gpuDevice;				// ָ���豸
	OCLASMCompiler * compiler;
	OCLBinaryCompiler * bcompiler;

	OCLStream * stream;	// command queue
	CodeObject * code_obj;
	KernelObject * kernel_obj;
	DispatchParam dispatch_param;
};
