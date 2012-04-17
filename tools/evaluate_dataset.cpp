// kate: replace-tabs off; indent-width 4; indent-mode normal
// vim: ts=4:sw=4:noexpandtab
/*

Copyright (c) 2010--2011,
François Pomerleau and Stephane Magnenat, ASL, ETHZ, Switzerland
You can contact the authors at <f dot pomerleau at gmail dot com> and
<stephane at magnenat dot net>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ETH-ASL BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "pointmatcher/PointMatcher.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <boost/format.hpp>
#include "boost/filesystem/path.hpp"
#include "boost/filesystem/operations.hpp"
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace PointMatcherSupport;


class EvaluateDataset
{
public:
	typedef PointMatcher<float> PM;
	typedef PM::TransformationParameters TP;
	typedef PM::DataPoints DataPoints;
	
	EvaluateDataset(const string& dataPath, const string& configPath, const string& evalFileName);
	~EvaluateDataset();

	void testAll();
private:
	void validateFileInfo(PM::FileInfo fileInfo);

	PM::FileInfoVector list;
	std::fstream resultFile;
	PM pm;
};


EvaluateDataset::EvaluateDataset(const string& evalFileName, const string& dataPath, const string& configPath):
	list(evalFileName, dataPath, configPath)
{
	string outputFileName = "evalResults.csv";
//#if BOOST_FILESYSTEM_VERSION >= 3
//	dataPath = boost::filesystem::path(fileName).parent_path().string();
//#else
//	dataPath = boost::filesystem::path(fileName).parent_path().file_string();
//#endif
	resultFile.open(outputFileName, fstream::out);
	
	if (!resultFile.good())
	{
		  cerr << "Error, invalid file " << outputFileName << endl;
		  abort();
	}
	
	//setLogger(pm.LoggerRegistrar.create("FileLogger"));
}

void EvaluateDataset::testAll()
{
	
	PM::DataPoints refCloud, readCloud;

	// Ensure that all required columns are there
	validateFileInfo(list[0]);

	// Start evaluation
	for(unsigned i=0; i < list.size(); i++)
	{
		// Load point clouds
		//TODO: handle vtk
		readCloud = PM::loadCSV(list[i].readingFileName);
		cout << "Reading cloud " << list[i].readingFileName << " loaded" << endl;
		refCloud = PM::loadCSV(list[i].referenceFileName);
		cout << "Reference cloud " << list[i].referenceFileName << " loaded" << endl;
	
		// Build ICP based on config file
		PM::ICP icp;
		ifstream ifs(list[i].configFileName);
		icp.loadFromYaml(ifs);

		const TP Tinit = list[i].initialTransformation;
		const TP Ttruth = list[i].groundTruthTransformation;
		
		bool failedToConverge = false;
		TP Tresult = TP::Identity(4,4);
		// Apply ICP
		try
		{
			Tresult = icp(readCloud, refCloud, Tinit);
		}
		catch (PM::ConvergenceError error)
		{
			cout << "ICP failed to converge" << endl;
			failedToConverge = true;
		}
		icp.inspector->addStat("failed", failedToConverge);

		// Define errors
		// TODO: only working in 3D up to now
		const TP Terr = Ttruth * Tresult.inverse();

		// Translation
		Eigen::Vector3f trans(Terr.topRightCorner(3,1));
		const float errTrans = trans.norm();
		icp.inspector->addStat("e_x", trans.x());
		icp.inspector->addStat("e_y", trans.y());
		icp.inspector->addStat("e_z", trans.z());
		icp.inspector->addStat("e_trans", errTrans);
		cout << "Error in translation: " << errTrans << " m" << endl;
		
		// Rotation
		Eigen::Quaternionf quat(Eigen::Matrix3f(Terr.topLeftCorner(3,3)));
		quat.normalize();
		const float errRot = 2 * acos(quat.normalized().w());
		icp.inspector->addStat("e_qx", quat.x());
		icp.inspector->addStat("e_qy", quat.y());
		icp.inspector->addStat("e_qz", quat.z());
		icp.inspector->addStat("e_qw", quat.w());
		icp.inspector->addStat("e_rot", errRot);
		cout << "Error in rotation: " << (errRot*180)/M_PI << " deg or " << errRot << " rad" << endl;
		
		// Output results
		if (i == 0)
		{
			icp.inspector->dumpStatsHeader(resultFile);
			resultFile << endl;
		}

		icp.inspector->dumpStats(resultFile);
		resultFile << endl;
	}
}

void EvaluateDataset::validateFileInfo(PM::FileInfo fileInfo)
{
	if(fileInfo.initialTransformation.rows() == 0)
	{
		cout << "Missing columns representing initial transformation \"iTxy\"" << endl;
		abort();
	}

	if(fileInfo.groundTruthTransformation.rows() == 0)
	{
		cout << "Missing columns representing ground truth transformation \"gTxy\"" << endl;
		abort();
	}

	if(fileInfo.readingFileName == "")
	{
		cout << "Missing column named \"reading\"" << endl;
		abort();
	}

	if(fileInfo.referenceFileName == "")
	{
		cout << "Missing column named \"reference\"" << endl;
		abort();
	}

	if(fileInfo.configFileName == "")
	{
		cout << "Missing column named \"config\"" << endl;
		abort();
	}
}

EvaluateDataset::~EvaluateDataset()
{
	resultFile.close();
}

/**
  * Code example for ICP evaluation taking pairs of scan with  
  * their global coordinates (first guest) and the ground truth coordinates 
  * to give evaluations of different ICP version
  */
int main(int argc, char *argv[])
{
	if (!(argc == 4))
	{
		cerr << "Error in command line, usage " << argv[0] << " experimentFileName.csv /path/To/Data/ /path/To/Config/ resultFileName.csv" << endl;
		abort();
	}	
	
	EvaluateDataset evaluation(argv[1], argv[2], argv[3]);
	evaluation.testAll();

	return 0;
}

