#include <any>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <array>
#include <atomic>
#include <string>
#include <optional>

template<size_t N>
class HashTable {
private:
    struct Node {
        std::string key;
        std::any value;
        std::shared_ptr<Node> next;
        
        Node(std::string &k, std::any &v): key(k), value(v), next(nullptr) {};
        Node(std::string &k, std::any &v, std::shared_ptr<Node> next): 
            key(k), 
            value(v), 
            next(next) 
        {};

    };
    
    struct Bucket {
        std::shared_mutex mutex;
        std::shared_ptr<Node> head;
        Bucket() : head(nullptr) {}
        
        bool insert(const std::string &key, const std::any &value){
            if(search(key) == std::nullopt){
                auto newNode = std::make_shared<Node>(Node(key, value, head));
                head = newNode;
                return true;
            }
            return false;
        }
        
        std::optional<std::any> search(const std::string &key){
            Node* prev = nullptr;
            Node* curr = head.get();
            if(curr != nullptr && curr->key == key){
                return curr->value;
            }
        
            while(curr != nullptr){
                if(curr->key == key){
                    return curr->value;
                }
                prev = curr;
                curr = curr->next;
            }
            return std::nullopt;
        }
        
        bool remove(const std::string &key){
            Node* prev = nullptr;
            Node* curr = head.get();
            if(curr != nullptr && curr->key == key){
                head = curr->next;
                delete curr;
                return true;
            }
        
            while(curr != nullptr){
                if(curr->key == key){
                    prev->next = curr->next;
                    delete curr;
                    return true;
                }
                prev = curr;
                curr = curr->next;
            }
            return false;
        }
        
    };
    
    std::array<Bucket, N> buckets;
    std::atomic<size_t> size_;
    std::hash<std::string> hasher;
    
public:
    HashTable(): size_(0) {}
    
    bool put(const std::string &key, const std::any &value){
        auto idx = hasher(key);
        std::unique_lock<std::shared_mutex> lock(buckets[idx].mutex);
        return buckets[idx].insert(key, value) ? (size_.fetch_add(1), true) : false;
    }
    
    std::optional<std::any> get(const std::string &key){
        auto idx = hasher(key);
        std::shared_lock<std::shared_mutex> lock(buckets[idx].mutex);
        return buckets[idx].search(key);
    }
    
    bool remove(const std::string &key){
        auto idx = hasher(key);
        std::unique_lock<std::shared_mutex> lock(buckets[idx].mutex);
        return buckets[idx].remove(key) ? (size_.fetch_sub(1), true) : false;
    }
    
    size_t size(){
        return size_.load();
    }
    
};
