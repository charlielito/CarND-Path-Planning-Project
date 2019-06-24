#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "splines.h"

// for convenience
using nlohmann::json;
using std::string;
using std::vector;

float MIN_OBS_DISTANCE = 30; // [m] Gap from car to obstacles
float DECEL = 0.324; // 0.324 is around 7.24m/s which is under 10m/s2 for jerk requirement
float ACCEL = 0.224; // 0.224 is around 5m/s which is under 10m/s2 for jerk requirement
float MAX_SPEED = 49.8; // Maximum speed limit
float LANE_WIDTH = 4.0; // Lane width


int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

  string line;
  while (getline(in_map_, line)) {
    std::istringstream iss(line);
    double x;
    double y;
    float s;
    float d_x;
    float d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }

  int lane = 1;
  double ref_vel = 0;

  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,
               &map_waypoints_dx,&map_waypoints_dy,&lane, &ref_vel]
              (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
               uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
          // Main car's localization Data
          double car_x = j[1]["x"];
          double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values 
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data, a list of all other cars on the same side 
          //   of the road.
          auto sensor_fusion = j[1]["sensor_fusion"];

          json msgJson;

          vector<double> next_x_vals;
          vector<double> next_y_vals;

          int previous_size = previous_path_x.size();

          bool current_lane_close=false;
          bool left_lane_close=false;
          bool right_lane_close=false;

          // If car in most left lane can't turn to the left
          if(lane==0){ 
            left_lane_close = true;
          }
          // If car in most right lane can't turn to the right
          else if(lane==2){
            right_lane_close = true;
          }

          if(previous_size>0){
            car_s=end_path_s;
          }

          //Iterate over the sensor fusion list to get the data(x,y,vx, vy, s, d) of all cars in the current lane
          for(int i=0;i<sensor_fusion.size();i++){
            // Get cars variables
            double d=sensor_fusion[i][6];
            double vx=sensor_fusion[i][3];
            double vy=sensor_fusion[i][4];
            double check_speed=sqrt(vx*vx+vy*vy);
            double check_car_s=sensor_fusion[i][5];
            check_car_s+=(double)previous_size*0.02*check_speed;

            //Check whether the car in the list is in the same lane as our car
            if( (d<LANE_WIDTH*lane+LANE_WIDTH) && (d>LANE_WIDTH*lane) ){
            // check for close enough cars ahead
            if(check_car_s>car_s && (check_car_s-car_s)<MIN_OBS_DISTANCE){
                current_lane_close=true;
              }
            }

            // check for close enough cars ahead and back
            if (abs(check_car_s-car_s) < MIN_OBS_DISTANCE){

              // check for left cars
              if( (d<LANE_WIDTH*(lane-1)+LANE_WIDTH) && (d>LANE_WIDTH*(lane-1)) && lane!=0 ){
                bool forward = false;
                bool backward = false;
                if (check_car_s>car_s)
                  forward = true;
                // for backward check only portion of the min distance
                else if (abs(check_car_s-car_s) < MIN_OBS_DISTANCE*0.4){
                  backward = true;
                }

                left_lane_close = forward || backward;
              }

              // check for right cars
              if( (d<LANE_WIDTH*(lane+1)+LANE_WIDTH) && (d>LANE_WIDTH*(lane+1)) && lane!=2 ){
                bool forward = false;
                bool backward = false;
                if (check_car_s>car_s)
                  forward = true;
                // for backward check only portion of the min distance
                else if (abs(check_car_s-car_s) < MIN_OBS_DISTANCE*0.4){
                  backward = true;
                }

                right_lane_close = forward || backward;
              }

              // left_lane_close = check_close_car(d, car_s, check_car_s, lane, 
              //       LANE_WIDTH, MIN_OBS_DISTANCE, -1, 0, left_lane_close );
              // right_lane_close = check_close_car(d, car_s, check_car_s, lane, 
              //       LANE_WIDTH, MIN_OBS_DISTANCE, 1, 2, right_lane_close );
              
            }
          }

          //when possible return to center lane
          if((lane==0 && !right_lane_close)||(lane==2 && !left_lane_close)){
            lane = 1;
          }
          
          //If the car in front is too close reduce the reference velocity at the rate of 7.24m per second
          if(current_lane_close){
            ref_vel-=DECEL;

            // Change lane heuristic (prefer change to right)
            if(!right_lane_close){
              lane += 1;
            }
            else if(!left_lane_close){
              lane -= 1;
            }
          }
          //If the velocity of the car is less than 49.5mph then gradually increase it at 5m per second
          else if(ref_vel<MAX_SPEED-ACCEL){
            ref_vel+=ACCEL;
          }

          // Print turning information
          std::cout<<"|car_d: "<< car_d<<"| L_Turn: "<<left_lane_close<<"| R_Turn: "<<right_lane_close<<"| C_Obs: "<<current_lane_close
            <<"| Lane: "<<lane<<"| Ref_vel: "<<ref_vel<< std::endl;

          // *********************** Control path stuff **********************************
          vector<double> ptsx;
          vector<double> ptsy;

          double ref_x=car_x;
          double ref_y=car_y;
          double ref_yaw=deg2rad(car_yaw);

          //If the previous path has no points left  then use current car x and y to calculate previous car x & y and add to pts list
          if(previous_size<2){

              double prev_car_x=car_x-cos(car_yaw);
              double prev_car_y=car_y-sin(car_yaw);
              ptsx.push_back(prev_car_x);
              ptsx.push_back(car_x);
              ptsy.push_back(prev_car_y);
              ptsy.push_back(car_y);
              
          }
          //If previous path has enough points add the last 2 points to the pts list
          else{
            ref_x=previous_path_x[previous_size-1];
            ref_y=previous_path_y[previous_size-1];

            double ref_prev_x=previous_path_x[previous_size-2];
            double ref_prev_y=previous_path_y[previous_size-2];
            ref_yaw=atan2(ref_y-ref_prev_y,ref_x-ref_prev_x);

            ptsx.push_back(ref_prev_x);
            ptsx.push_back(ref_x);

            ptsy.push_back(ref_prev_y);
            ptsy.push_back(ref_y);
          }
          //Predict future waypoints at distances of 30, 60 and 90m and add to points list
          vector<double> next_wp_30=getXY(car_s+30,2+4*lane,map_waypoints_s,map_waypoints_x,map_waypoints_y);
          vector<double> next_wp_60=getXY(car_s+60,2+4*lane,map_waypoints_s,map_waypoints_x,map_waypoints_y);
          vector<double> next_wp_90=getXY(car_s+90,2+4*lane,map_waypoints_s,map_waypoints_x,map_waypoints_y);

          ptsx.push_back(next_wp_30[0]);
          ptsx.push_back(next_wp_60[0]);
          ptsx.push_back(next_wp_90[0]);

          ptsy.push_back(next_wp_30[1]);
          ptsy.push_back(next_wp_60[1]);
          ptsy.push_back(next_wp_90[1]);
          //Convert pts lists from map co-ordinates to car co-ordinates
          for(int i=0;i<ptsx.size();i++){
            double shift_x=ptsx[i]-ref_x;
            double shift_y=ptsy[i]-ref_y;

            ptsx[i]=(shift_x*cos(-ref_yaw)-shift_y*sin(-ref_yaw));
            ptsy[i]=(shift_x*sin(-ref_yaw)+shift_y*cos(-ref_yaw));
          }

          tk::spline s;
          //Use the converted pts list (waypoints) in spline to create a trajectory
          s.set_points(ptsx,ptsy);
          //Push remaining points from previous path to the next waypoints list
          for(int i=0;i<previous_path_x.size();i++){
            next_x_vals.push_back(previous_path_x[i]);
            next_y_vals.push_back(previous_path_y[i]);
          }

          double target_x=30;
          //Get the y point for the given x point which is 30 m ahead of the current position from the spline
          double target_y=s(target_x);
          double target_dist=sqrt(target_x*target_x+target_y*target_y);
          double x_add_on=0;

          

          //Keeping the remaining points from previous path , calculate 50-remaining points using the spline
          for(int i=0; i<50-previous_path_x.size(); i++){
            //Find the number of divisions that the trajectory has to be split into
            double N=target_dist/(0.02*ref_vel/2.24);
            //Calculate remaining x and y points (using spline)
            double x_point=x_add_on+target_x/N;
            double y_point=s(x_point);

            x_add_on=x_point;

            double x_ref=x_point;
            double y_ref=y_point;
            //Convert back from car co-ordinates to map co-ordinates
            x_point=x_ref*cos(ref_yaw)-y_ref*sin(ref_yaw);
            y_point=x_ref*sin(ref_yaw)+y_ref*cos(ref_yaw);

            x_point+=ref_x;
            y_point+=ref_y;
            //Push the calculated points to the next waypoints list
            next_x_vals.push_back(x_point);
            next_y_vals.push_back(y_point);

          }


          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"control\","+ msgJson.dump()+"]";

          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }  // end "telemetry" if
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }  // end websocket if
  }); // end h.onMessage

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  
  h.run();
}