#pragma once

struct weather_data;
struct room_data;

// Base class that encapsulates the singleton implementation
template <class T>
class singleton {
public:
    static void create()
    {
        static T theInstance;
        m_pInstance = &theInstance;
    }

    static T& instance()
    {
        if (!m_pInstance) {
            if (m_bDestroyed) {
                on_instance_destroyed();
            } else {
                on_instance_not_created();
            }
        }
        return m_pInstance;
    }

protected:
    virtual ~singleton() { }

    virtual void on_instance_destroyed() {};
    virtual void on_instance_not_created() {};

private:
    // Deleted functions.
    // Don't put definitions in here so trying to do them will cause a compile error.
    singleton<T>(const singleton<T>& other);
    singleton<T>& operator=(const singleton<T>&);

    static T* m_pInstance;
    static bool m_bDestroyed;
};

// Base class that encapsulates the singleton implementation.
// This class allows for weather_data and room_data in the singleton.
template <class T>
class world_singleton {
public:
    static void create(const weather_data& weather, const room_data* world)
    {
        static T theInstance(&weather, world);
        m_pInstance = &theInstance;
    }

    // Whether create has made the instance yet.
    static bool is_created() { return m_pInstance != nullptr; }

    static T& instance()
    {
        if (!m_pInstance) {
            if (m_bDestroyed) {
                // on_instance_destroyed();
            } else {
                // on_instance_not_created();
            }
        }
        return *m_pInstance;
    }

protected:
    world_singleton()
        : m_weather(0)
        , m_world(0)
    {
    }
    world_singleton(const weather_data* weather, const room_data* world)
        : m_weather(weather)
        , m_world(world)
    {
    }

    // The instance is a function-local static, destroyed at exit; is_created() is false after.
    virtual ~world_singleton()
    {
        m_pInstance = nullptr;
        m_bDestroyed = true;
    }

    const weather_data& get_weather() const { return *m_weather; }
    const room_data* get_world() const { return m_world; }

    virtual void on_instance_destroyed() {};
    virtual void on_instance_not_created() {};

private:
    // Deleted functions.
    // Don't put definitions in here so trying to do them will cause a compile error.
    world_singleton<T>(const world_singleton<T>& other);
    world_singleton<T>& operator=(const world_singleton<T>&);

    static T* m_pInstance;
    static bool m_bDestroyed;

    const weather_data* m_weather;
    const room_data* m_world;
};
