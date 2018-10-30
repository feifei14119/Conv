#pragma once

#include "BasicClass.h"

namespace AutoTune
{
	using namespace std;
	/************************************************************************/
	/* ��������					                                             */
	/************************************************************************/
	typedef struct SearchParamType
	{
		SearchParamType(std::string name)
		{
			Name = name;
		}
		SearchParamType()
		{
		}

		std::string Name;
		std::vector<int> ValueArray;
		int CurrIdx;
		int CurrValue;
		int BestIdx;
		int BestValue;
		int MinValue;
		int MaxValue;
		int Step;
		int ValueNum;

		SearchParamType operator=(SearchParamType &p)
		{
			Name = p.Name;
			CurrValue = p.CurrValue;
			CurrIdx = p.CurrIdx;
			MinValue = p.MinValue;
			MaxValue = p.MaxValue;
			Step = p.Step;
			ValueNum = p.ValueNum;

			for (int i = 0; i < p.ValueArray.size(); i++)
			{
				int val = p.ValueArray[i];
				ValueArray.push_back(val);
			}

			return *this;
		}
	} T_SearchParam;

	class SearchSpace
	{
	public:
		SearchSpace()
		{
			searchParams = new std::vector<T_SearchParam>;
		}

		~SearchSpace()
		{
			delete searchParams;
		}

	public:
		int ParamNum = 0;

	private:
		std::vector<T_SearchParam> * searchParams;	// �������������б�

		int searchParamIdx = 0;
		bool moveCurrIdx = true;
		int getParamIdx = 0;

	public:
		/************************************************************************/
		/* ��ȡһ���µĲ������													*/
		/************************************************************************/
		E_ReturnState GetNexComb()
		{
			T_SearchParam * currParam;
			currParam = &((*searchParams)[searchParamIdx]);

			// �������: ����Ѿ�ָ�����һ���������������ָ��,���������
			if ((searchParamIdx >= ParamNum - 1) && (currParam->CurrIdx >= currParam->ValueNum - 1) && moveCurrIdx)
			{
				moveCurrIdx = true;
				searchParamIdx = 0;
				return E_ReturnState::FAIL;
			}

			// ������ǰ����ָ��
			bool moveNextIdx;
			if (moveCurrIdx)
			{
				if (currParam->CurrIdx >= currParam->ValueNum - 1)
				{
					currParam->CurrIdx = 0;
					moveNextIdx = true;
				}
				else
				{
					currParam->CurrIdx++;
					moveNextIdx = false;
				}

				currParam->CurrValue = currParam->ValueArray[currParam->CurrIdx];
			}

			// ������һ�����: ��ǰ�����������һ������
			if (searchParamIdx >= ParamNum - 1)
			{
				moveCurrIdx = true;
				searchParamIdx = 0;
				return E_ReturnState::SUCCESS;
			}

			// ������һ�����
			searchParamIdx++;
			moveCurrIdx = moveNextIdx;
			GetNexComb();
		}

		/************************************************************************/
		/* ��¼��ǰ�������														*/
		/************************************************************************/
		E_ReturnState RecordBestComb()
		{
			for (int i = 0; i < ParamNum; i++)
			{
				(*searchParams)[i].BestIdx = (*searchParams)[i].CurrIdx;
				(*searchParams)[i].BestValue = (*searchParams)[i].CurrValue;
			}
		}

		/************************************************************************/
		/* ���һ���µĲ����б�													*/
		/************************************************************************/
		E_ReturnState AddOneParam(T_SearchParam * param)
		{
			T_SearchParam *newParam = new T_SearchParam();
			*newParam = *param;

			if (newParam->ValueArray.size() == 0)
			{
				if (newParam->Step == 0)
				{
					return E_ReturnState::FAIL;
				}

				int len = (int)ceil((newParam->MaxValue - newParam->MinValue) / newParam->Step);

				if (len <= 0)
				{
					return E_ReturnState::FAIL;
				}

				int val = newParam->MinValue;
				for (int i = 0; i < len; i++)
				{
					newParam->ValueArray.push_back(val);
					val + newParam->Step;
				}
			}

			newParam->CurrIdx = 0;
			newParam->CurrValue = newParam->ValueArray[0];
			newParam->ValueNum = newParam->ValueArray.size();

			searchParams->push_back(*newParam);
			ParamNum++;

			return E_ReturnState::SUCCESS;
		}

		/************************************************************************/
		/* ��ȡ��һ������															*/
		/************************************************************************/
		T_SearchParam * GetOneParam()
		{
			if (searchParams == NULL)
			{
				getParamIdx = 0;
				return NULL;
			}

			if (getParamIdx >= searchParams->size())
			{
				getParamIdx = 0;
				return NULL;
			}

			getParamIdx++;
			return &(*searchParams)[getParamIdx - 1];
		}
	};
}
