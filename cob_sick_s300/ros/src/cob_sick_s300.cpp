/****************************************************************
 *
 * Copyright (c) 2010
 *
 * Fraunhofer Institute for Manufacturing Engineering	
 * and Automation (IPA)
 *
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *
 * Project name: care-o-bot
 * ROS stack name: cob_driver
 * ROS package name: cob_sick_s300
 * Description:
 *								
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *			
 * Author: Florian Weisshardt, email:florian.weisshardt@ipa.fhg.de
 * Supervised by: Christian Connette, email:christian.connette@ipa.fhg.de
 *
 * Date of creation: Jan 2010
 * ToDo:
 *
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *	 * Redistributions of source code must retain the above copyright
 *	   notice, this list of conditions and the following disclaimer.
 *	 * Redistributions in binary form must reproduce the above copyright
 *	   notice, this list of conditions and the following disclaimer in the
 *	   documentation and/or other materials provided with the distribution.
 *	 * Neither the name of the Fraunhofer Institute for Manufacturing 
 *	   Engineering and Automation (IPA) nor the names of its
 *	   contributors may be used to endorse or promote products derived from
 *	   this software without specific prior written permission.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License LGPL as 
 * published by the Free Software Foundation, either version 3 of the 
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License LGPL for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public 
 * License LGPL along with this program. 
 * If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************/

//##################
//#### includes ####

// standard includes
//--

// ROS includes
#include <ros/ros.h>

// ROS message includes
#include <sensor_msgs/LaserScan.h>
#include <diagnostic_msgs/DiagnosticArray.h>

// ROS service includes
//--

// external includes
#include "cob_sick_s300/SickS300.hpp"
#include <cob_sick_s300/ScannerSickS300.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/thread.hpp>

#define ROS_LOG_FOUND

//####################
//#### node class ####
class NodeClass
{
	//
	public:
		  
		ros::NodeHandle nh;   
		// topics to publish
		ros::Publisher topicPub_LaserScan;
        ros::Publisher topicPub_Diagnostic_;
		
		// topics to subscribe, callback is called for new messages arriving
		//--
		
		// service servers
		//--
			
		// service clients
		//--
		
		// global variables
		std::string port;
		int baud, scan_id, publish_frequency;
		bool inverted;
		double scan_duration, scan_cycle_time;
		std::string frame_id;
		ros::Time syncedROSTime;
		unsigned int syncedSICKStamp;
		bool syncedTimeReady;

		// Constructor
		NodeClass()
		{
			// create a handle for this node, initialize node
			nh = ros::NodeHandle("~");
			
			if(!nh.hasParam("port")) ROS_WARN("Used default parameter for port");
			nh.param("port", port, std::string("/dev/ttyUSB0"));
			
			if(!nh.hasParam("baud")) ROS_WARN("Used default parameter for baud");
			nh.param("baud", baud, 500000);
			
			if(!nh.hasParam("scan_id")) ROS_WARN("Used default parameter for scan_id");
			nh.param("scan_id", scan_id, 7);
			
			if(!nh.hasParam("inverted")) ROS_WARN("Used default parameter for inverted");
			nh.param("inverted", inverted, false);
			
			if(!nh.hasParam("frame_id")) ROS_WARN("Used default parameter for frame_id");
			nh.param("frame_id", frame_id, std::string("/base_laser_link"));
			
			if(!nh.hasParam("scan_duration")) ROS_WARN("Used default parameter for scan_duration");
			nh.param("scan_duration", scan_duration, 0.025); //no info about that in SICK-docu, but 0.025 is believable and looks good in rviz
			
			if(!nh.hasParam("scan_cycle_time")) ROS_WARN("Used default parameter for scan_cycle_time");
			nh.param("scan_cycle_time", scan_cycle_time, 0.040); //SICK-docu says S300 scans every 40ms

			if (!nh.hasParam("publish_frequency")) ROS_WARN("Used default parameter for publish_frequency");
			nh.param("publish_frequency", publish_frequency, 12); //Hz

			syncedSICKStamp = 0;
			syncedROSTime = ros::Time::now();
			syncedTimeReady = false;

			// implementation of topics to publish
			topicPub_LaserScan = nh.advertise<sensor_msgs::LaserScan>("scan", 1);
			topicPub_Diagnostic_ = nh.advertise<diagnostic_msgs::DiagnosticArray>("/diagnostics", 1);

			// implementation of topics to subscribe
			//--
			
			// implementation of service servers
			//--
		}
		
		// Destructor
		~NodeClass() 
		{
		}

		// topic callback functions 
		// function will be called when a new message arrives on a topic
		//--

		// service callback functions
		// function will be called when a service is querried
		//--
		
		// other function declarations
		void publishLaserScan(std::vector<double> vdDistM, std::vector<double> vdAngRAD, std::vector<double> vdIntensAU, unsigned int iSickTimeStamp, unsigned int iSickNow)
		{
			// fill message
			int start_scan, stop_scan;
			int num_readings = vdDistM.size(); // initialize with max scan size
			start_scan = 0;
			stop_scan = vdDistM.size();
			
			// Sync handling: find out exact scan time by using the syncTime-syncStamp pair:
			// Timestamp: "This counter is internally incremented at each scan, i.e. every 40 ms (S300)"
			if(iSickNow != 0) {
				syncedROSTime = ros::Time::now() - ros::Duration(scan_cycle_time); 
				syncedSICKStamp = iSickNow;
				syncedTimeReady = true;
				
				ROS_DEBUG("Got iSickNow, store sync-stamp: %d", syncedSICKStamp);
			}
			
			// create LaserScan message
			sensor_msgs::LaserScan laserScan;
			if(syncedTimeReady) {
				double timeDiff = (int)(iSickTimeStamp - syncedSICKStamp) * scan_cycle_time;
				laserScan.header.stamp = syncedROSTime + ros::Duration(timeDiff);
				
				ROS_DEBUG("Time::now() - calculated sick time stamp = %f",(ros::Time::now() - laserScan.header.stamp).toSec());
			} else {
				laserScan.header.stamp = ros::Time::now();
			}
			
			// fill message
			laserScan.header.frame_id = frame_id;
			laserScan.angle_increment = vdAngRAD[start_scan + 1] - vdAngRAD[start_scan];
			laserScan.range_min = 0.001;
			laserScan.range_max = 30.0;
			laserScan.time_increment = (scan_duration) / (vdDistM.size());

			// rescale scan
			num_readings = vdDistM.size();
			laserScan.angle_min = vdAngRAD[start_scan]; // first ScanAngle
			laserScan.angle_max = vdAngRAD[stop_scan - 1]; // last ScanAngle
   			laserScan.ranges.resize(num_readings);
			laserScan.intensities.resize(num_readings);

			
			// check for inverted laser
			if(inverted) {
				// to be really accurate, we now invert time_increment
				// laserScan.header.stamp = laserScan.header.stamp + ros::Duration(scan_duration); //adding of the sum over all negative increments would be mathematically correct, but looks worse.
				laserScan.time_increment = - laserScan.time_increment;
			} else {
				laserScan.header.stamp = laserScan.header.stamp - ros::Duration(scan_duration); //to be consistent with the omission of the addition above
			}

			for(int i = 0; i < (stop_scan - start_scan); i++)
			{
				if(inverted)
				{
					laserScan.ranges[i] = vdDistM[stop_scan-1-i];
					laserScan.intensities[i] = vdIntensAU[stop_scan-1-i];
				}
				else
				{
					laserScan.ranges[i] = vdDistM[start_scan + i];
					laserScan.intensities[i] = vdIntensAU[start_scan + i];
				}
			}
        
			// publish Laserscan-message
			topicPub_LaserScan.publish(laserScan);
			
			//Diagnostics
			diagnostic_msgs::DiagnosticArray diagnostics;
			diagnostics.status.resize(1);
			diagnostics.status[0].level = 0;
			diagnostics.status[0].name = nh.getNamespace();
			diagnostics.status[0].message = "sick scanner running";
			topicPub_Diagnostic_.publish(diagnostics);
			}

				void publishError(std::string error_str) {
					diagnostic_msgs::DiagnosticArray diagnostics;
					diagnostics.status.resize(1);
					diagnostics.status[0].level = 2;
					diagnostics.status[0].name = nh.getNamespace();
					diagnostics.status[0].message = error_str;
					topicPub_Diagnostic_.publish(diagnostics);     
				}
};

//#######################
//#### main programm ####
int main(int argc, char** argv)
{
	// initialize ROS, spezify name of node
	ros::init(argc, argv, "sick_s300");
	
	NodeClass nodeClass;
	brics_oodl::SickS300 sickS300;
	brics_oodl::Errors errors;

	bool bOpenScan = false;
	
	unsigned int iSickTimeStamp = 0, iSickNow = 0;
	std::vector<double> vdDistM, vdAngRAD, vdIntensAU;

	brics_oodl::LaserScannerConfiguration config;

	config.devicePath = nodeClass.port.c_str(); // Device path of the Sick S300
	config.scannerID = nodeClass.scan_id;

	switch (nodeClass.baud) {
	case 9600:
		config.baud = brics_oodl::BAUD_9600;
		break;
	case 38400:
		config.baud = brics_oodl::BAUD_38400;
		break;
	case 115200:
		config.baud = brics_oodl::BAUD_115200;
		break;
		break;
	case 500000:
		config.baud = brics_oodl::BAUD_500K;
		break;
	default:
		config.baud = brics_oodl::BAUD_UNKNOWN;
		break;
	}

	if (!sickS300.setConfiguration(config, errors)) {
		errors.printErrorsToConsole();
	}

	while (!bOpenScan) {
	ROS_INFO("Opening scanner... (port:%s)", nodeClass.port.c_str());
	//bOpenScan = SickS300.open(nodeClass.port.c_str(), iBaudRate, iScanId);
	bOpenScan = sickS300.open(errors);
	// check, if it is the first try to open scanner
	if (!bOpenScan) {
		ROS_ERROR("...scanner not available on port %s. Will retry every second.", nodeClass.port.c_str());
		nodeClass.publishError("...scanner not available on port");
	}
	sleep(1); // wait for scan to get ready if successfull, or wait befor retrying
	}
	ROS_INFO("...scanner opened successfully on port %s", nodeClass.port.c_str());

	// main loop
	ros::Rate loop_rate(nodeClass.publish_frequency); // Hz
	while (nodeClass.nh.ok()) {
	// read scan
	ROS_DEBUG("Reading scanner...");
	/* Acquire the most recent scan from the Sick */
	if (sickS300.getData(vdDistM, vdAngRAD, vdIntensAU, iSickTimeStamp, iSickNow, errors)) {
		ROS_DEBUG("...read LaserScan from scanner successfully");
		// publish LaserScan
		ROS_DEBUG("...publishing LaserScan message");
		nodeClass.publishLaserScan(vdDistM, vdAngRAD, vdIntensAU, iSickTimeStamp, iSickNow);
	} else {
		ROS_DEBUG("...no Scan available");
	}
	// sleep and waiting for messages, callbacks	
	ros::spinOnce();
	loop_rate.sleep();
	}
	return 0;
}
