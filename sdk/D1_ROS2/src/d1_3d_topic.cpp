#include "d1_3d_topic.h"

namespace D1
{
    using namespace CygLiDARD1;

    void Topic_3D::publishScanImage(std::string frame_id_, rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher_image_,
                                    uint16_t *payload_data_buffer_3d)
    {
        sensor_msgs::msg::Image::SharedPtr message_image(new sensor_msgs::msg::Image);

        message_image->header.stamp = rclcpp::Clock().now();
        message_image->header.frame_id = frame_id_;
        message_image->height = static_cast<uint32_t>(Sensor::Height);
        message_image->width  = static_cast<uint32_t>(Sensor::Width);
        message_image->encoding = sensor_msgs::image_encodings::MONO16;
        message_image->step = message_image->width * sizeof(uint16_t);
        message_image->is_bigendian = false;
        message_image->data.resize(message_image->height * message_image->step);

        depth_data = reinterpret_cast<uint16_t*>(&message_image->data[0]);

        for (int y = 0; y < message_image->height; y++)
        {
            for (int x = 0; x < message_image->width; x++)
            {
                buffer_index = x + (y * message_image->width);

                if (payload_data_buffer_3d[buffer_index] < Distance::Mode3D::Maximum_Depth_3D)
                {
                    depth_data[buffer_index] = payload_data_buffer_3d[buffer_index];
                }
                else depth_data[buffer_index] = bad_point;
            }
        }
        publisher_image_->publish(*message_image);
    }

    void Topic_3D::publishPoint3D(std::string frame_id_, rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr &publisher_point_3d_,
                                  PointCloudMaker &pointcloud_maker, uint16_t *distance_buffer_3d_)
    {
        pcl::PointCloud<pcl::PointXYZRGBA>::Ptr pointcloud_3d(new pcl::PointCloud<pcl::PointXYZRGBA>);

        pointcloud_3d->header.frame_id = frame_id_;
        pointcloud_3d->is_dense = false;
        pointcloud_3d->width  = Sensor::Width;
        pointcloud_3d->height = Sensor::Height;
        pointcloud_3d->points.resize(cyg_driver::DATA_LENGTH_3D);

        for (buffer_index = 0; buffer_index < Sensor::Height * Sensor::Width; buffer_index++)
        {
            raw_distance = distance_buffer_3d_[buffer_index];

            if(raw_distance < Distance::Mode3D::Maximum_Depth_3D)
            {
                pointcloud_maker.calcPointCloud(raw_distance, buffer_index, camera_coordinate_x, camera_coordinate_y, camera_coordinate_z);

                pointcloud_3d->points[buffer_index].x = camera_coordinate_z * MM2M;
                pointcloud_3d->points[buffer_index].y = -camera_coordinate_x * MM2M;
                pointcloud_3d->points[buffer_index].z = -camera_coordinate_y * MM2M;
                color_change_with_height = pointcloud_maker.color_map[((int)camera_coordinate_y / 2) % pointcloud_maker.color_map.size()];
                pointcloud_3d->points[buffer_index].rgb = *reinterpret_cast<float *>(&color_change_with_height);
                pointcloud_3d->points[buffer_index].a = 255;
            }
            else
            {
                pointcloud_3d->points[buffer_index].x = 0;
                pointcloud_3d->points[buffer_index].y = 0;
                pointcloud_3d->points[buffer_index].z = 0;
                pointcloud_3d->points[buffer_index].rgb = 0;
                pointcloud_3d->points[buffer_index].a = 0;
            }
        }
        pcl_conversions::toPCL(rclcpp::Clock().now(), pointcloud_3d->header.stamp);

        sensor_msgs::msg::PointCloud2 message_point_cloud_3d;
        pcl::toROSMsg(*pointcloud_3d, message_point_cloud_3d);
        publisher_point_3d_->publish(message_point_cloud_3d);
    }
}
