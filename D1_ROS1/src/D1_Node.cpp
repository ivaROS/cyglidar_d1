#include "D1_Node.h"

D1_Node::D1_Node()
{
    topic_2d    = new Topic2D();
    topic_3d    = new Topic3D();
    cyg_driver  = new CYG_Driver();
    serial_port = new CYG_SerialUart();

    topic_2d->initPublisher(nh.advertise<sensor_msgs::LaserScan>  ("scan",       10),
                            nh.advertise<sensor_msgs::PointCloud2>("scan_2D",    10));

    topic_3d->initPublisher(nh.advertise<sensor_msgs::Image>      ("scan_image", 10),
                            nh.advertise<sensor_msgs::PointCloud2>("scan_3D",    10));

    received_buffer[0].packet_data = first_total_packet_data;
    received_buffer[1].packet_data = second_total_packet_data;

    initConfiguration();

    future = exit_signal.get_future();
    double_buffer_thread = std::thread(&D1_Node::doublebufferThread, this);
    publish_thread       = std::thread(&D1_Node::publishThread, this);
}

D1_Node::~D1_Node()
{
    exit_signal.set_value();

    double_buffer_thread.join();
    publish_thread.join();

    delete topic_2d;
    delete topic_3d;
    delete cyg_driver;
    delete serial_port;

    topic_2d    = nullptr;
    topic_3d    = nullptr;
    cyg_driver  = nullptr;
    serial_port = nullptr;
}

void D1_Node::connectBoostSerial()
{
    try
    {
        serial_port->openSerialPort(port_number, baud_rate_mode);

        requestPacketData();
    }
    catch (const boost::system::system_error& ex)
    {
        ROS_ERROR("[BOOST SERIAL ERROR] %s", ex.what());
    }
}

void D1_Node::disconnectBoostSerial()
{
    serial_port->closeSerialPort();
    ROS_ERROR("[PACKET REQUEST] STOP");
}

void D1_Node::loopCygParser()
{
    number_of_data = serial_port->getPacketLength(packet_structure);

    for (uint16_t i = 0; i < number_of_data; i++)
    {
        parser_return = cyg_driver->CygParser(received_buffer[double_buffer_index].packet_data, packet_structure[i]);

        if(parser_return == D1_Const::CHECKSUM_PASSED)
        {
            publish_done_flag |= (1 << double_buffer_index);

            double_buffer_index++;
            double_buffer_index &= 1;

            convertInfoData(&received_buffer[0]);
        }
    }
}

void D1_Node::initConfiguration()
{
    ros::NodeHandle priv_nh("~");

    priv_nh.param<std::string>("port_number",       port_number,       "/dev/ttyUSB0");
    priv_nh.param<int>        ("baud_rate",         baud_rate_mode,    0);
    priv_nh.param<std::string>("frame_id",          frame_id,          "laser_frame");
    priv_nh.param<int>        ("run_mode",          run_mode,          ROS_Const::MODE_DUAL);
    priv_nh.param<int>        ("duration_mode",     duration_mode,     ROS_Const::PULSE_AUTO);
    priv_nh.param<int>        ("duration_value",    duration_value,    10000);
    priv_nh.param<int>        ("frequency_channel", frequency_channel, 0);

    topic_2d->assignLaserScan(frame_id);
    topic_2d->assignPCL2D(frame_id);
    topic_3d->assignImage(frame_id);
    topic_3d->assignPCL3D(frame_id);
}

void D1_Node::requestPacketData()
{
    serial_port->requestDeviceInfo();
    ros::Duration(3.0).sleep();
    // sleep for 3s, by requsting the info data.

    serial_port->requestRunMode(run_mode, mode_notice);
    ROS_INFO("[PACKET REQUEST] %s", mode_notice.c_str());

    serial_port->requestDurationControl(run_mode, duration_mode, duration_value);
    ros::Duration(1.0).sleep();
    ROS_INFO("[PACKET REQUEST] PULSE DURATION : %d", duration_value);
    // sleep for a sec, by requsting the duration

    serial_port->requestFrequencyChannel(frequency_channel);
    ROS_INFO("[PACKET REQUEST] FREQUENCY CH.%d", frequency_channel);
}

void D1_Node::convertData(received_data_buffer* _received_buffer)
{
    if (_received_buffer->packet_data[D1_Const::PAYLOAD_HEADER] == D1_Const::PACKET_HEADER_2D)
    {
        start_time_scan_2d = ros::Time::now() - ros::Duration(0.00048);

        cyg_driver->getDistanceArray2D(&_received_buffer->packet_data[D1_Const::PAYLOAD_INDEX], distance_buffer_2d);
        publish_data_state = ROS_Const::PUBLISH_2D;
    }
    else if (_received_buffer->packet_data[D1_Const::PAYLOAD_HEADER] == D1_Const::PACKET_HEADER_3D)
    {
        cyg_driver->getDistanceArray3D(&_received_buffer->packet_data[D1_Const::PAYLOAD_INDEX], distance_buffer_3d);
        publish_data_state = ROS_Const::PUBLISH_3D;
    }
}

void D1_Node::convertInfoData(received_data_buffer* _received_buffer)
{
    if (_received_buffer->packet_data[D1_Const::PAYLOAD_HEADER] == D1_Const::PACKET_HEADER_DEVICE_INFO && info_flag == false)
    {
        ROS_INFO("[F/W VERSION] %d.%d.%d", _received_buffer->packet_data[6], _received_buffer->packet_data[7],  _received_buffer->packet_data[8]);
        ROS_INFO("[H/W VERSION] %d.%d.%d", _received_buffer->packet_data[9], _received_buffer->packet_data[10], _received_buffer->packet_data[11]);
        info_flag = true;
    }
}

void D1_Node::processDoubleBuffer()
{
    if(publish_done_flag & 0x1)
    {
        publish_done_flag &= (~0x1);
        convertData(&received_buffer[0]);
    }
    else if(publish_done_flag & 0x2)
    {
        publish_done_flag &= (~0x2);
        convertData(&received_buffer[1]);
    }
}

void D1_Node::runPublish()
{
    if (publish_data_state == ROS_Const::PUBLISH_3D)
    {
        topic_3d->publishPoint3D(distance_buffer_3d);
        topic_3d->publishScanImage(distance_buffer_3d);

        publish_data_state = ROS_Const::PUBLISH_DONE;
    }
    else if (publish_data_state == ROS_Const::PUBLISH_2D)
    {
        topic_2d->applyPointCloud2D(distance_buffer_2d);
        topic_2d->publishPoint2D();

        topic_2d->publishScanLaser(start_time_scan_2d, distance_buffer_2d);

        publish_data_state = ROS_Const::PUBLISH_DONE;
    }
}

void D1_Node::doublebufferThread()
{
    do
    {
        processDoubleBuffer();
        status = future.wait_for(std::chrono::seconds(0));
    } while (status == std::future_status::timeout);
}

void D1_Node::publishThread()
{
    do
    {
        runPublish();
        status = future.wait_for(std::chrono::seconds(0));
    } while (status == std::future_status::timeout);
}
