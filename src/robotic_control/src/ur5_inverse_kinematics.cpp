// custom
#include<robot_control/inverse_kinematics_interfaces.h>
#include<robot_control/ur5_inverse_kinematics.h>
#include<robot_control/util.h>

// cpp
#include<algorithm>
#include<array>
#include<vector>
#include<iostream>

void convertAngle(float &angle)
{
  if(angle > M_PIf )
  {
    angle = M_PIf * 2.0F - angle;
  }
  else if(angle < -M_PIf)
  {
    angle += M_PIf * 2.0F;
  }
}

UR5RobotArm::UR5RobotArm()
{
    // 填写dh表 a/mm,d/mm,alpha,theta
    dh_table_.push_back(std::array<float,4>{0.0f,0.0f,0.0f,0.0f});
    dh_table_.push_back(std::array<float,4>{0.0f,13.40f,-M_PIf/2.0f,0.0f});
    dh_table_.push_back(std::array<float,4>{42.50f,0.0f,0.0f,0.0f});
    dh_table_.push_back(std::array<float,4>{39.20f,0.0f,0.0f,0.0f});
    dh_table_.push_back(std::array<float,4>{0.0f,-10.0f,M_PIf/2.0f,0.0f});
    dh_table_.push_back(std::array<float,4>{0.0f,10.0f,-M_PIf/2.0f,0.0f});

    // 填写关节电机角度限制
    angle_limit_.push_back(std::array<float,2>());
    angle_limit_.push_back(std::array<float,2>());
    angle_limit_.push_back(std::array<float,2>());
    angle_limit_.push_back(std::array<float,2>());
    angle_limit_.push_back(std::array<float,2>());
    angle_limit_.push_back(std::array<float,2>());
}

// TODO：尝试多种逆解求法
std::vector<Solutions<6>> UR5RobotArm::inverseKinematics(const Pose end_pose)
{
    Solutions<6> solution;
    std::vector<Solutions<6>> solutions;
    Eigen::Matrix4f end_to6,temp_pose;
    end_to6 << 1,0,0,0,
               0,1,0,0,
               0,0,1,10.0f,
               0,0,0,1;

    temp_pose = end_pose * end_to6.inverse();

    float theta1;
    float step = M_PIf / 720.0f;

    std::vector<float> thetas;

    for(int i = 0; i < 1440;i++ )
    {
      float angle = step * i;
      float value = temp_pose(0,3) * cos(angle) + temp_pose(1,3) * sin(angle);
      if(std::abs(value - dh_table_[1][1]) < 0.2f )
      {
         thetas.emplace_back(angle);
      }
    }

    std::sort(thetas.begin(),thetas.end(),[&thetas](float a,float b){ return a < b;});

    for(int i = 0;i < 2;i++){
    //theta1 = thetas[thetas.size()-1];
    // theta1 = thetas[0];
    theta1 = (i == 1) ? thetas[thetas.size()-1] : thetas[0];
    if(theta1 > M_PIf * 1.5f)
       theta1 -= M_PIf * 2.0f;
    theta1 -= M_PIf/2.0f;
    // std::cout<<theta1<<std::endl;
    Eigen::Matrix4f P = axisTransform(dh_table_[0][0],dh_table_[0][1],dh_table_[0][2],theta1).inverse()
                        * temp_pose;
    
    float theta234,theta5,theta6;

    static auto fun = [&]()
    {
      mutipleSolution(theta234,P(0,3),P(2,3),theta5);
      convertAngle(theta1);
      convertAngle(theta5);
      convertAngle(theta6);
      // theta5 = theta5 < 0 ? -theta5 -M_PIf : theta5;
      // theta6 = theta6 < 0 ? -theta6 -M_PIf : theta6;
      dh_table_[0][3] = theta1;
      dh_table_[4][3] = theta5;
      dh_table_[5][3] = theta6;

      for(int index = 0 ; index < 6;index++)
          solution.theta[index] = dh_table_[index][3];

      solutions.emplace_back(solution);
    };

    if(pow(P(1,2),2) < 1e-4)
    {
      theta5 = M_PIf/2.0f;
      theta234 = atan2(P(2,2),-P(0,2));
      theta6 = atan2(-P(1,1),P(1,0));
      std::cout<<"kao"<<std::endl;
    }
    else if(pow(P(1,2)+1,2) < 1e-3)//等下补充
    {
      std::cout<<"wow"<<std::endl;
    }
    else
    {
      // 情况一:
      theta5 = atan2(sqrt(pow(P(1,0),2)+pow(P(1,1),2)),P(1,2));
      float s5 = sin(theta5);
      theta234= atan2(P(2,2)/s5,P(0,2)/-s5);
      theta6 = atan2(P(1,1)/-s5,P(1,0)/s5);
      
      fun();

      theta5 = atan2(-sqrt(pow(P(1,0),2)+pow(P(1,1),2)),P(1,2));
      s5 = sin(theta5);
      theta234 = atan2(P(2,2)/s5,P(0,2)/-s5);
      theta6 = atan2(P(1,1)/-s5,P(1,0)/s5);
      fun();
    }
  }
    return solutions;
}

bool UR5RobotArm::isRightSolution(const Solutions<6> solution,const Pose end_pose)
{
    bool flag{false};

    for(int index = 0;index < 6;index++)
    {
        dh_table_[index][3] = solution.theta[index];
    }

    Eigen::Matrix4f reach_pose = forwardKinematics(dh_table_);

    if(checkSolutions(end_pose,reach_pose,4.0,1.0))
       flag = true;

    return flag;
} 


void UR5RobotArm::mutipleSolution(float theta234,float x,float z,float theta5)
{
   const static int max_iterations = 35;
    Eigen::Vector<float,3> diff; //保存差值
    Eigen::Vector<float,3> current; //保存上一次状态
    Eigen::Vector<float,3> target; // 目标状态
    Eigen::Vector<float,3> angle; //保存角度值
    diff << 0.0f,0.0f,0.0f;
    current<< 78.6703,-5.27069,0.2f;
    target << x,z,theta234;
    angle << -0.2,0.3f,0.1f;
    Eigen::Matrix<float,3,3> jacobian_matrix; //雅各比矩阵5x5
    jacobian_matrix << 0.0f,0.0f,0.0f,
                       0.0f,0.0f,0.0f,
                       1.0f,1.0f,1.0f;
    
    const float len1 = std::abs(dh_table_[2][0]);
    const float len2 = std::abs(dh_table_[3][0]);
    const float len3 = std::abs(dh_table_[4][1]);

    // 初始化状态
    float position_error,angle_error;
    for(int k = 0; k < max_iterations;k++)
    {
      position_error = std::abs(target[0]-current[0])+std::abs(target[1]-current[1]);
      angle_error = std::abs(target[2]-current[2]);
      if(position_error > 0.4f || angle_error > 0.06f){//满足迭代条件则继续迭代，直到达到收敛条件
      // 根据当前角度求出对应的雅各比矩阵
      jacobian_matrix(0,0) = -len1 * sin(angle(0)) - len2 * sin(angle(0)+angle(1))
                                      +len3 * cos(angle(0)+angle(1)+angle(2));
      jacobian_matrix(0,1) = -len2 * sin(angle(0)+angle(1))
                                      +len3 * cos(angle(0)+angle(1)+angle(2));    
      jacobian_matrix(0,2) = len3 * cos(angle(0)+angle(1)+angle(2));   
      jacobian_matrix(1,0) = -len1 * cos(angle(0)) - len2 * cos(angle(0)+angle(1))-
                                      len3 * sin(angle(0)+angle(1)+angle(2));
      jacobian_matrix(1,1) = -len2 * cos(angle(0)+angle(1))-
                                      len3 * sin(angle(0)+angle(1)+angle(2));
      jacobian_matrix(1,2) = -len3 * sin(angle(0)+angle(1)+angle(2));  

      diff(0) = target(0) - current(0);
      diff(1) = target(1) - current(1);
      diff(2) = target(2) - current(2);
      Eigen::Vector<float,3> delta = jacobian_matrix.inverse() * diff;
      // 每个关节加上变化量
          angle(0) += delta(0);
          angle(1) += delta(1);
          angle(2) += delta(2);
      }
      else 
      {
        std::cout<<"we get the solution at "<<k+1<<" times"<<std::endl;
        std::cout<<"possition error is "<<position_error<<std::endl;
        std::cout<<"total angle error is "<<angle_error<<std::endl;
        break;
      }
      // 正向运动学求得对应齐次矩阵
      Eigen::Matrix4f transform_matrix = Eigen::Matrix4f::Identity();
      transform_matrix *= axisTransform(std::array<float,4>{0.0f,0.0f,-M_PIf/2.0f,angle(0)});
      transform_matrix *= axisTransform(std::array<float,4>{42.50f,0.0f,0.0f,angle(1)});
      transform_matrix *= axisTransform(std::array<float,4>{39.20f,0.0f,0.0f,angle(2)});
      transform_matrix *= axisTransform(std::array<float,4>{0.0f,-10.0f,M_PIf/2.0f,theta5});

      current(0) = transform_matrix(0,3);
      current(1) = transform_matrix(2,3);
      current(2) = angle(0) + angle(1) + angle(2);
    }

    dh_table_[1][3] = angle(0);
    dh_table_[2][3] = angle(1);
    dh_table_[3][3] = angle(2);
    // Eigen::Matrix4f pose = forwardKinematics(dh_table_);
    // std::cout<<"------Reach Matrix------"<<std::endl;
    // for(int f = 0;f < 4;f++)
    // {
    //   std::cout<<"第"<<f+1<<"行"<<std::endl;
    //   for(int j = 0;j < 4;j++)
    //   {
    //     std::cout<<pose(f,j)<<" ";
    //   }
    //   std::cout<<std::endl;
    // }
    // float theta2 = angle(0) / 2.0f / M_PIf;
    // float theta3 = angle(1) / 2.0f / M_PIf;
    // float theta4 = angle(2) / 2.0f / M_PIf;
    // theta2 = (theta2 - std::trunc(theta2))*2.0f*M_PIf;
    // theta3 = (theta3 - std::trunc(theta3))*2.0f*M_PIf;
    // theta4 = (theta4 - std::trunc(theta4))*2.0f*M_PIf;


    // convertAngle(theta2);
    // convertAngle(theta3);
    // convertAngle(theta4);

    // dh_table_[1][3] = theta2;
    // dh_table_[2][3] = theta3;
    // dh_table_[3][3] = theta4;
}

