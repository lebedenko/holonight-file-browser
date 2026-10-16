qt_add_executable(file-browser-tests reader_test.cpp primitives_test.cpp)
target_link_libraries(file-browser-tests PRIVATE HolonightFileBrowser::Core Qt6::Test GTest::gtest)
add_test(NAME file-browser-core COMMAND file-browser-tests)
set_tests_properties(file-browser-core PROPERTIES TIMEOUT 30)
add_test(NAME file-browser-installed-core COMMAND ${CMAKE_COMMAND}
  -DPROVIDER_BUILD=${CMAKE_BINARY_DIR} -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/consumer
  -DQUICK=OFF "-DPROVIDER_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
  "-DPROVIDER_QML_PATH=${QML_IMPORT_PATH}" -P ${CMAKE_CURRENT_SOURCE_DIR}/installed-consumer.cmake)
if(BUILD_FILE_BROWSER_QUICK)
  add_test(NAME file-browser-installed-quick COMMAND ${CMAKE_COMMAND}
    -DPROVIDER_BUILD=${CMAKE_BINARY_DIR} -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/consumer
    -DQUICK=ON "-DPROVIDER_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DPROVIDER_QML_PATH=${QML_IMPORT_PATH}" -P ${CMAKE_CURRENT_SOURCE_DIR}/installed-consumer.cmake)
  add_test(NAME file-browser-installed-qml-plugin COMMAND ${CMAKE_COMMAND}
    -DPROVIDER_BUILD=${CMAKE_BINARY_DIR} -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/consumer
    -DQUICK=ON -DPLUGIN_ONLY=ON "-DPROVIDER_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DPROVIDER_QML_PATH=${QML_IMPORT_PATH}" -P ${CMAKE_CURRENT_SOURCE_DIR}/installed-consumer.cmake)
endif()

if(BUILD_FILE_BROWSER_SEARCH)
  qt_add_executable(browser-search-tests service_main.cpp search_test.cpp fuzzy_matcher_test.cpp)
  target_link_libraries(browser-search-tests PRIVATE HolonightFileBrowser::Search Qt6::Test GTest::gtest)
  add_test(NAME browser-search COMMAND browser-search-tests)
endif()
if(BUILD_FILE_BROWSER_PREVIEW)
  qt_add_executable(browser-preview-tests service_main.cpp text_lines_test.cpp text_line_model_test.cpp native_preview_test.cpp)
  target_link_libraries(browser-preview-tests PRIVATE HolonightFileBrowser::Preview Qt6::Test GTest::gtest)
  if(BUILD_FILE_BROWSER_PREVIEW_QUICK)
    target_link_libraries(browser-preview-tests PRIVATE HolonightFileBrowser::PreviewQuick)
    target_compile_definitions(browser-preview-tests PRIVATE BROWSER_TEST_PREVIEW_QUICK)
  endif()
  add_test(NAME browser-preview COMMAND browser-preview-tests)
endif()

foreach(component Search Storage Preview PreviewQuick Markdown)
  if(TARGET HolonightFileBrowser::${component})
    add_test(NAME browser-installed-${component} COMMAND ${CMAKE_COMMAND}
      -DPROVIDER_BUILD=${CMAKE_BINARY_DIR} -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/component-consumer
      -DCOMPONENT=${component} "-DPROVIDER_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
      "-DPROVIDER_QML_PATH=${QML_IMPORT_PATH}" -P ${CMAKE_CURRENT_SOURCE_DIR}/installed-consumer.cmake)
  endif()
endforeach()

if(BUILD_FILE_BROWSER_STORAGE)
  qt_add_executable(browser-storage-contract-tests service_main.cpp storage_contract_test.cpp)
  target_link_libraries(browser-storage-contract-tests PRIVATE HolonightFileBrowser::Storage GTest::gtest)
  target_compile_definitions(browser-storage-contract-tests PRIVATE BROWSER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME browser-storage-contract COMMAND browser-storage-contract-tests)
endif()
