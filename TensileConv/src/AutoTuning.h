#pragma once

#include <math.h>
#include <algorithm>
#include "../common/ff_utils.h"

namespace TensileConv{
namespace AutoTune{
/************************************************************************/
/* ��������					                                             */
/************************************************************************/
typedef enum SearchMethordEnum
{
	SEARCH_BRUTE = 0,
	SEARCH_GENETIC = 1
} E_SearchMethord;

typedef struct SearchParamType
{
	SearchParamType(std::string name = "")
	{
		Name = name;
	}

	std::string Name;

	int MinValue;
	int MaxValue;
	int Step;
	int ValueNum;
	std::vector<int> ValueArray;

	int CurrIdx;
	int CurrValue;
	int BestIdx;
	int BestValue;

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
		return E_ReturnState::SUCCESS;
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

class GeneticSearch
{
protected:
#define SURV_CHANCE (0.7)		// 10��������7��
#define CORSS_CHANCE (0.8)
#define MUTATE_CHANCE (0.1)

	// һ������λ��
	typedef struct GeneLocusType
	{
		std::string Name;
		int Idx;
		int Val;
	} T_GeneLocus;
	// һ��Ⱦɫ�弴һ������,�����������λ��
	typedef struct ChromosomeType
	{
		std::vector<T_GeneLocus> Chrom;
	} T_Chromosome;

public:
#define POP_SIZE		(10) // ÿ����Ⱥ10������
#define MAX_GENERATION	(50) // 50����Ⱥ
	GeneticSearch() { genePool = new std::vector<T_SearchParam*>; }
	~GeneticSearch() 
	{
		delete genePool;
		delete &population;
		delete &newPopulation;
		delete &populationValue;
		delete &surviveChance;
		delete &rouletteChance;
	}

	void InitGeneticSearch()
	{
		initPopulation();
	}

	E_ReturnState AddOneGenePool(T_SearchParam * param)
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

		genePool->push_back(newParam);

		return E_ReturnState::SUCCESS;
	}

	/************************************************************************/
	/* ��¼��ǰȾɫ��															*/
	/************************************************************************/
	void RecordCurrChrom()
	{
		for (T_SearchParam * pGene : *genePool)
		{
			pGene->BestIdx = pGene->CurrIdx;
			pGene->BestValue = pGene->CurrValue;
		}
	}

	/************************************************************************/
	/* ��¼һ����Ч��������Լ������											*/
	/************************************************************************/
	void SetOneChromValue(double value)
	{
		setObjChromValue(value);

		// ָ����һ����Ⱥ����
		currObjChromIdx++;

		// ���������Ⱥ���������ɣ��������Ⱥ����
		if (currObjChromIdx == POP_SIZE)
		{
			currObjChromIdx = 0;
			populationFit();
			populationChance();
			sellectPopulation();
			crossover();
			mutation();
			population = newPopulation;
		}
	}

	/************************************************************************/
	/* ���ɻ��һ��������													*/
	/************************************************************************/
	std::vector<T_SearchParam*> * GetOneChrom()
	{
		genRandChrom();
		return genePool;
	}

protected:
	std::vector<T_SearchParam*> * genePool;	// �����,���������������б�
	std::vector<T_Chromosome> population;
	int currObjChromIdx = 0;

	// Ϊһ������λ���������һ��ֵ
	void genRandGene(T_SearchParam *pGene)
	{
		pGene->CurrIdx = randInt10(0, pGene->ValueNum);
		pGene->CurrValue = pGene->ValueArray[pGene->CurrIdx];
	}
	// Ϊһ�������������һ��Ⱦɫ��
	void genRandChrom()
	{
		for (T_SearchParam *pGene : *genePool)
		{
			genRandGene(pGene);
		}
	}
	// ��ʼ����Ⱥ
	void initPopulation()
	{
		// ��ʼ����Ⱥ����
		population.resize(POP_SIZE);
		newPopulation.resize(POP_SIZE);
		populationValue.resize(POP_SIZE);
		surviveChance.resize(POP_SIZE);
		rouletteChance.resize(POP_SIZE);
		currObjChromIdx = 0;

		// ��ʼ��Ⱦɫ�峤��
		for (int objIdx = 0; objIdx < population.size(); objIdx++)
		{
			population[objIdx].Chrom.resize(genePool->size());
			newPopulation[objIdx].Chrom.resize(genePool->size());
		}
	}

	// ��Ⱥ����
	std::vector<double> populationValue;
	void setObjChromValue(double value)
	{
		populationValue[currObjChromIdx] = value;
		for (int loci = 0; loci < genePool->size(); loci++)
		{
			population[currObjChromIdx].Chrom[loci].Name = (*genePool)[loci]->Name;
			population[currObjChromIdx].Chrom[loci].Idx = (*genePool)[loci]->CurrIdx;
			population[currObjChromIdx].Chrom[loci].Val = (*genePool)[loci]->CurrValue;
		}
	}

	// ��Ⱥ�ĸ�����
	void populationFit()
	{
		int thresholdPos;
		double thresholdValue;

		std::sort(populationValue.begin(), populationValue.end());
		thresholdPos = int(POP_SIZE * SURV_CHANCE);
		thresholdValue = populationValue[thresholdPos];
		
		for (int i = 0; i < POP_SIZE; i++)
		{
			if (populationValue[i] >= thresholdValue)
			{
				populationValue[i] = 0;
			}
		}
	}

	// ��Ⱥ������	
	std::vector<double> surviveChance;
	std::vector<double> rouletteChance;
	void populationChance()
	{
		double totalValue = 0;
		for (int i = 0; i < POP_SIZE; i++)
		{
			totalValue += populationValue[i];
		}

		// ������
		for (int i = 0; i < POP_SIZE; i++)
		{
			surviveChance[i] = populationValue[i] / totalValue;
		}

		// ת�̸���
		for (int objIdx = 0; objIdx < POP_SIZE; objIdx++)
		{
			double sum_chance = 0;
			for (int i = 0; i <= objIdx; i++)
			{
				sum_chance += surviveChance[i];
			}
			rouletteChance[objIdx] = sum_chance;
		}
	}

	// ��Ⱥѡ��
	std::vector<T_Chromosome> newPopulation;
	void sellectPopulation()
	{
		double chance = 0;
		for (int newIdx = 0; newIdx < POP_SIZE; newIdx++)
		{
			chance = 1.0 * rand() / RAND_MAX;

			for (int oldIdx = 0; oldIdx < POP_SIZE; oldIdx++)
			{
				if (chance < rouletteChance[oldIdx])
				{
					newPopulation[newIdx] = population[oldIdx];
					break;
				}
			}
		}
	}

	// �����ӽ�
	void crossover()
	{
		double chance = 0;
		int cpyLen = 0;
		T_GeneLocus tempGene;

		for (int objIdx = 0; objIdx < POP_SIZE - 1; objIdx++)
		{
			chance = 1.0 * rand() / RAND_MAX;
			if (chance < CORSS_CHANCE)
			{
				cpyLen = (int)randInt10(0, genePool->size());
				for (int geneIdx = cpyLen; geneIdx < genePool->size(); geneIdx++)
				{
					tempGene = newPopulation[objIdx].Chrom[geneIdx];
					newPopulation[objIdx].Chrom[geneIdx] = newPopulation[objIdx + 1].Chrom[geneIdx];
					newPopulation[objIdx + 1].Chrom[geneIdx] = tempGene;
				}
			}
		}
	}

	// ����ͻ��
	void mutation()
	{
		double chance = 0;
		int mutPos = 0;

		for (int objIdx = 0; objIdx < POP_SIZE - 1; objIdx++)
		{
			chance = 1.0 * rand() / RAND_MAX;
			if (chance < MUTATE_CHANCE)
			{
				mutPos = (int)randInt10(0, genePool->size());
				genRandGene((*genePool)[mutPos]);
				newPopulation[objIdx].Chrom[mutPos].Idx = (*genePool)[mutPos]->CurrIdx;
				newPopulation[objIdx].Chrom[mutPos].Val = (*genePool)[mutPos]->CurrValue;
			}
		}
	}
};
}
}
