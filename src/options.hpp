// Copyright (c) 2025 Graphcore Ltd. All rights reserved.

#pragma once

#include <boost/program_options.hpp>
#include <iostream>

boost::program_options::variables_map
parseOptions(int argc, char** argv, boost::program_options::options_description&& desc) {
  namespace po = boost::program_options;

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  if (vm.count("help")) {
    std::cout << desc << "\n";
    throw std::runtime_error("Show help");
  }

  po::notify(vm);
  return vm;
}
