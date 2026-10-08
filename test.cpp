#include <iostream>
#include <map>
#include <flat_map>
#include <unordered_map>
#include <chrono>

struct Order {
    int id;
    int price;
    int quantity;
};

int main() {
    const int n = 10;

    std::map<int, Order> map;
    

    std::unordered_map<int, Order> u_map;
    
    //std::flat_map<int, Order*> f_map;

    for (int i = 0; i < n; i++) {
        Order order;
        order.id = i;
        order.price = i * 5;
        order.quantity = 1;

        map[i].id = order.id;
        map[i].price = order.price;
        map[i].quantity = order.quantity;
        //u_map[i] = order;
        //f_map[i] = &order;
    }

    std::cout << map[3].id << std::endl;

    return 0;
}