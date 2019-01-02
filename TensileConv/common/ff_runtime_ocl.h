﻿#pragma once

#include <vector>
#include <CL/opencl.h>

#include "ff_basic.h"
#include "ff_ocl_helper.h"
#include "ff_file_opt.h"
#include "ff_log.h"

namespace feifei 
{
	class KernelOCL;
	class CmdQueueOCL;

	class DeviceOCL
	{
	public:
		DeviceOCL(cl_device_id id)
		{
			deviceId = id;
			getDeviceInfo();
		}
		cl_device_id DeviceId() { return deviceId; }
		cl_device_id * pDeviceId() { return &deviceId; }

		void AddCmdQueue(CmdQueueOCL * q) { queues.push_back(q); }
		void AddKernel(KernelOCL * k) { kernels.push_back(k); }
		T_DeviceInfo * DeviceInfo() { return &deviceInfo; }

	protected:
		cl_platform_id platformId;
		cl_device_id deviceId;
		T_DeviceInfo deviceInfo;
		std::vector<CmdQueueOCL*> queues;
		std::vector<KernelOCL*> kernels;

		void getDeviceInfo();
	};

	class KernelOCL
	{
	public:
		KernelOCL(char * content, std::string kernelName, E_ProgramType type, DeviceOCL * dev)
		{
			switch (type)
			{
			case PRO_OCL_FILE:		programFile = content;		break;
			case PRO_OCL_STRING:	programSrc = content;		break;
			case PRO_GAS_FILE:		programFile = content;		break;
			case PRO_GAS_STRING:	programSrc = content;		break;
			case PRO_BIN_FILE:		programFile = content;		break;
			case PRO_BIN_STRING:	programSrc = content;		break;
			}
			programType = type;
			device = dev;
			this->kernelName = kernelName;
			argsCnt = 0;
		}

		E_ReturnState CreatKernel(cl_context *ctx);
		std::string BuildOption = "";
		cl_device_id DeviceId() { return device->DeviceId(); }
		cl_kernel Kernel() { return kernel; }

		E_ReturnState SetArgs() { return E_ReturnState::SUCCESS; }
		template <typename T, typename... Ts>
		E_ReturnState SetArgs(T head, Ts... rest)
		{
			//uint argsCnt = sizeof...(rest);
			int errNum;
			
			errNum = clSetKernelArg(kernel, argsCnt, sizeof(head), (const void*)head);
			if (errNum != CL_SUCCESS)
			{
				clErrInfo(errNum);
				ERR("Failed set kernel arg %d.", argsCnt);
			}
			argsCnt++;
			SetArgs(rest...);
		}

	protected:
		DeviceOCL * device;

		cl_program program;
		E_ProgramType programType;
		std::string programFile;
		char * programSrc;
		size_t programSize;

		cl_kernel kernel;
		std::string kernelName;
		std::string kernelFile;
		uint argsCnt;

		E_ReturnState creatKernelFromOclFile(cl_context *ctx);
		E_ReturnState creatKernelFromOclString(cl_context *ctx);
		E_ReturnState creatKernelFromGasFile(cl_context *ctx);
		E_ReturnState creatKernelFromGasString(cl_context *ctx);
		E_ReturnState creatKernelFromBinFile(cl_context *ctx);
		E_ReturnState creatKernelFromBinString(cl_context *ctx);
		E_ReturnState buildKernel();
		E_ReturnState dumpKernel();
		E_ReturnState dumpProgram();
	};

	class CmdQueueOCL
	{
	public:
		CmdQueueOCL(DeviceOCL * dev)
		{
			device = dev;
		}
		~CmdQueueOCL()
		{
			clReleaseCommandQueue(cmdQueue);
		}
		cl_device_id DeviceId() { return device->DeviceId(); }
		E_ReturnState CreatQueue(cl_context *ctx, bool enProf);
		cl_command_queue Queue() { return cmdQueue; }
		E_ReturnState MemCopyH2D(cl_mem d_mem, void * h_mem, size_t byteNum);
		E_ReturnState MemCopyD2H(void * h_mem, cl_mem d_mem, size_t byteNum);
		E_ReturnState Launch(KernelOCL *k, dim3 global_sz, dim3 group_sz, cl_event * evt_creat = NULL);
		void Finish() { clFinish(cmdQueue); }

	private:
		cl_command_queue_properties prop = 0;
		DeviceOCL * device;
		cl_command_queue cmdQueue;
	};

	class RuntimeOCL
	{
	private:
		static RuntimeOCL * pInstance;
		RuntimeOCL()
		{
			compiler = "clang";
			SetKernelTempDir("./kernel");
		}

	protected:
		cl_platform_id platformId;
		T_PlatformInfo platformInfo;
		cl_context context;
		std::vector<DeviceOCL*> devices;
		DeviceOCL * selDevice;

		E_ReturnState initPlatform();
		void getPlatformInfo();

		std::string kernelTempDir;
		std::string compiler;

	public:
		static RuntimeOCL * GetInstance();
		~RuntimeOCL()
		{
			for (int i = 0; i < DevicesCnt(); i++)
			{
				clReleaseDevice(devices[i]->DeviceId());
			}

			clReleaseContext(context);

			if (RuntimeOCL::pInstance)
				pInstance = nullptr;
		}

		uint DevicesCnt() { return devices.size(); }
		DeviceOCL * Device() { return selDevice; }
		E_ReturnState SellectDevice(uint devNum) { selDevice = devices[devNum]; }
		CmdQueueOCL * CreatCmdQueue(bool enProf = false, int devNum = -1);

		KernelOCL * CreatKernel(char * content, std::string kernelName, E_ProgramType type, int devNum = -1);
		void SetKernelTempDir(std::string dir)
		{
			kernelTempDir = dir;
			ensure_dir(kernelTempDir.c_str());
		}
		std::string KernelTempDir() { return kernelTempDir; }
		std::string Compiler() { return compiler; }

		cl_mem DevMalloc(size_t byteNum);
		void DevFree(cl_mem d_mem) { clReleaseMemObject(d_mem); }

		double GetProfilingTime(cl_event * evt);
	};
}
