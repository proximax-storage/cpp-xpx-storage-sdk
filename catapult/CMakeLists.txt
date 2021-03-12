cmake_minimum_required(VERSION 3.2)
project(catapult)

file(GLOB crypto
	"crypto/*.cpp"
	"crypto/*.h")

file(GLOB ionet
	"ionet/*.cpp"
	"ionet/*.h")

file(GLOB model
	"model/*.cpp"
	"model/*.h")

file(GLOB net
	"net/*.cpp"
	"net/*.h")

file(GLOB thread
	"thread/*.cpp"
	"thread/*.h")

file(GLOB utils
	"utils/*.cpp"
	"utils/*.h")


add_library(catapult ${crypto} ${ionet} ${model} ${net} ${thread} ${utils})
set_property(TARGET catapult PROPERTY POSITION_INDEPENDENT_CODE ON)
