#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/ndarray.h>

#include <string>

#include "../test_server/InterfaceServer.hpp"
#include <opencv2/core/core.hpp>

namespace nb = nanobind;
using namespace nb::literals;

NB_MODULE(gui_server, m) {

    m.def("set_log_level", &InterfaceServer::setLogLevel);

    nb::class_<InterfaceServer::State>(m, "State")
        .def(nb::init<>())
        .def("__repr__", &InterfaceServer::State::toString)
        .def_rw("prompt", &InterfaceServer::State::prompt)
        .def_rw("value", &InterfaceServer::State::value)
        .def_rw("stop", &InterfaceServer::State::stop)
        .def_rw("steps", &InterfaceServer::State::steps)
        .def_rw("is_playing", &InterfaceServer::State::isPlaying);

    nb::class_<InterfaceServer>(m, "InterfaceServer")
        .def(nb::init<int>(), "port"_a)
        .def("consume_state", &InterfaceServer::consumeState)
        .def("get_state", &InterfaceServer::getState, nb::rv_policy::reference)
        .def("state_changed", &InterfaceServer::stateChanged)
        .def("start", &InterfaceServer::start)
        .def("initialise_video_stream", &InterfaceServer::initialiseVideoStream,
             "width"_a, "height"_a)
        .def("stop", &InterfaceServer::stop)
        .def("update_progress", &InterfaceServer::updateProgress,
             "step"_a, "total_steps"_a)
        .def("send_image", [](InterfaceServer& self, nb::ndarray<nb::numpy, uint8_t, nb::shape<-1, -1, 3>> array, bool convertToBGR) {
            // Convert numpy array to cv::Mat
            int height = array.shape(0);
            int width = array.shape(1);
            cv::Mat image(height, width, CV_8UC3, array.data());
            if (convertToBGR) {
                cv::cvtColor(image, image, cv::COLOR_RGB2BGR);
            }
            self.sendImage(image);
        }, "image"_a, "convert_to_bgr"_a);

    m.doc() = "Extension that exposes a graphical user interface server to Python.";
}
