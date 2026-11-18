"""Waypoint 到着時に SetBool を受けて処理を開始・停止する最小例。"""

import rclpy
from rclpy.node import Node
from std_srvs.srv import SetBool


class ArrivalServiceNode(Node):
    def __init__(self):
        super().__init__('arrival_service_node')
        self.declare_parameter('task_id', 'A')
        self.declare_parameter('service_name', '/mission/control_A')
        self.task_id = self.get_parameter('task_id').value
        service_name = self.get_parameter('service_name').value
        self.active = False
        self.work_timer = self.create_timer(1.0, self.do_work)
        self.work_timer.cancel()
        self.create_service(SetBool, service_name, self.control_task)
        self.get_logger().info(f'タスク {self.task_id}: {service_name} を待機中')

    def control_task(self, request, response):
        if request.data and not self.active:
            self.active = True
            self.work_timer.reset()
            self.get_logger().info(f'タスク {self.task_id} を開始')
        elif not request.data and self.active:
            self.work_timer.cancel()
            self.active = False
            self.get_logger().info(f'タスク {self.task_id} を停止')
        response.success = True
        response.message = '動作中' if self.active else '停止中'
        return response

    def do_work(self):
        # 実際の処理に置き換える。コールバックは短時間で終える。
        self.get_logger().info(f'タスク {self.task_id} が動作中')


def main():
    rclpy.init()
    node = ArrivalServiceNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
