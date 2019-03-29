#pragma once

#include <string>
#include <iostream>
#include <fstream>

#include "ff_basic.h"

#define INFO(fmt,...)		feifei::PrintInfo(fmt,##__VA_ARGS__)
#define	LOG(fmt,...)		feifei::PrintLog(fmt,##__VA_ARGS__)
#define WARN(fmt,...)		feifei::PrintWarning(__FILE__,__LINE__,fmt,##__VA_ARGS__)
#define ERR(fmt,...)		do{feifei::PrintError(__FILE__,__LINE__,fmt,##__VA_ARGS__);return E_ReturnState::RTN_ERR;}while(0)
#define FATAL(fmt,...)		feifei::PrintFatal(__FILE__,__LINE__,fmt,##__VA_ARGS__)

namespace feifei
{
	// ����ָ���
	void PrintSeperator(const char c, std::ostream *sm = &std::cout);
	// cout���
	void PrintInfo(const char * format, ...);
	void PrintInfo(std::string msg, ...);
	// ��ʱ�����clog���
	void PrintLog(const char * format, ...);
	void PrintLog(std::string msg, ...);
	// ��ʱ����ʹ���λ�õ�clog���
	void PrintWarning(const char *file, int line, const char * format, ...);
	void PrintWarning(const char *file, int line, std::string msg, ...);
	// ��ʱ����ʹ���λ�ò����ش����cerr���
	E_ReturnState PrintError(const char *file, int line, const char * format, ...);
	E_ReturnState PrintError(const char *file, int line, std::string msg, ...);
	// ��ʱ����ʹ���λ�ò���ֹ�����cerr���
	void PrintFatal(const char *file, int line, const char * format, ...);
	void PrintFatal(const char *file, int line, std::string msg, ...);

	class LogFile
	{
	public:
		LogFile(std::string file_name,bool isNewFile = true);
		~LogFile();
		void Log(const char * format, ...);
		void Log(std::string msg, ...);

	protected:
		void ensureLogDir();
		std::string log_dir;
		std::string file_name;
		std::ofstream * fstream; 
		char * PrintBuffer;
	};
}
