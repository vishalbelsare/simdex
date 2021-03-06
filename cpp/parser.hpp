//
//  parser.hpp
//  SimDex
//
//  Created by Geet Sethi on 10/24/16.
//  Copyright © 2016 Geet Sethi. All rights reserved.
//

#ifndef parser_hpp
#define parser_hpp

#include <string>

template <typename T>
T *parse_weights_csv(const std::string filename, const int num_rows,
                     const int num_cols);
uint32_t *parse_ids_csv(const std::string filename, const int num_rows);

#endif /* parser_hpp */
