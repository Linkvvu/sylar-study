#pragma once

#include <memory>

namespace sylar {

	template <typename T, int PlaceHolder = 1>
	class Singleton {
	public:
		static T& GetInstance();
	};



	template <typename T, int PlaceHolder = 1>
	class SingletonPtr {
	public:
		static std::shared_ptr<T> GetInstance();
	};

} // namespace sylar

template<typename T, int PlaceHolder>
inline T& sylar::Singleton<T, PlaceHolder>::GetInstance() {
	static T ls_instance;
	return ls_instance;
}

template<typename T, int PlaceHolder>
inline std::shared_ptr<T> sylar::SingletonPtr<T, PlaceHolder>::GetInstance() {
	auto ls_shared_p_instance = std::make_shared<T>();
	return ls_shared_p_instance;
}

