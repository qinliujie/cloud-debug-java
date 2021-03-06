#include "yaml_data_visibility_config_reader.h"

#include "jni_proxy_yamlconfigparser.h"
#include "jni_utils.h"

namespace devtools {
namespace cdbg {

// Config file to search for via ClassLookupPath.readApplicationResource, which
// is currently documented in ClassPathLookup.java
static constexpr char kResourcePath[] = "debugger-config.yaml";

// Reads the debugger yaml config.
//
// Returns true (success) if:
//   - Data for one file was found.  Sets config to file contents
//   - No config file is found. Sets config to ""
// Returns false (error) if there were multiple configurations found.
static bool ReadYamlConfig(
    ClassPathLookup* class_path_lookup,
    string* config,
    string* error) {
  std::set<string> files =
      class_path_lookup->ReadApplicationResource(kResourcePath);

  if (files.size() > 1) {
    LOG(ERROR) << "Multiple " << kResourcePath << " files found." << "  Found "
               << files.size() << " files.";
    *error = "Multiple debugger-config.yaml files found";
    return false;
  }

  if (files.empty()) {
    // No configuration file was provided
    LOG(INFO) << kResourcePath << " was not found.  Using default settings.";
    *config = "";
  } else {
    *config = *files.begin();
  }

  return true;
}


// Parses the string yaml_config that contains a yaml configuration.
// Adds data to config.
//
// Does not alter config if a parsing error occurs.
//
// Returns true if parse was sucessful, false otherwise.
static bool ParseYamlConfig(
    const string& yaml_config,
    GlobDataVisibilityPolicy::Config* config,
    string* error) {
  // Gather all needed data here.  Do not alter config until all data has been
  // collected without error.
  ExceptionOr<JniLocalRef> config_parser =
      jniproxy::YamlConfigParser()->NewObject(yaml_config);
  if (config_parser.HasException()) {
    LOG(ERROR) << "Exception creating YAML config parser object: "
               << FormatException(config_parser.GetException());
    *error = "Errors parsing debugger-config.yaml";
    return false;
  }

  ExceptionOr<JniLocalRef>
      blacklist_patterns = jniproxy::YamlConfigParser()->getBlacklistPatterns(
          config_parser.GetData().get());

  if (blacklist_patterns.HasException()) {
    LOG(ERROR) << "Exception getting blacklist patterns: "
               << FormatException(blacklist_patterns.GetException());
    *error = "Error building blacklist patterns";
    return false;
  }

  ExceptionOr<JniLocalRef> blacklist_exception_patterns =
      jniproxy::YamlConfigParser()->getBlacklistExceptionPatterns(
          config_parser.GetData().get());

  if (blacklist_exception_patterns.HasException()) {
    LOG(ERROR) << "Exception getting blacklist exception patterns: "
               << FormatException(blacklist_exception_patterns.GetException());
    *error = "Error building blacklist exception patterns";
    return false;
  }

  // The code below, which does change the config, should have no error paths.
  // Otherwise we might leave the caller with a partially-modified
  // configuration.
  std::vector<string> blacklist_patterns_cpp =
      JniToNativeStringArray(blacklist_patterns.GetData().get());

  for (const string& glob_pattern : blacklist_patterns_cpp) {
    config->blacklists.Add(glob_pattern);
  }

  std::vector<string> blacklist_exception_patterns_cpp =
      JniToNativeStringArray(blacklist_exception_patterns.GetData().get());

  for (const string& glob_pattern : blacklist_exception_patterns_cpp) {
    config->blacklist_exceptions.Add(glob_pattern);
  }

  return true;
}


GlobDataVisibilityPolicy::Config ReadYamlDataVisibilityConfiguration(
    ClassPathLookup* class_path_lookup) {
  GlobDataVisibilityPolicy::Config config;

  string yaml_config;
  string error;
  if (!ReadYamlConfig(class_path_lookup, &yaml_config, &error)) {
    config.parse_error = error;
    return config;
  }

  if (!yaml_config.empty()) {
    if (!ParseYamlConfig(yaml_config, &config, &error)) {
      config.parse_error = error;
      return config;
    }
  }

  // Prepare both blacklists and blacklist_exceptions for lookup processing.
  config.blacklists.Prepare();
  config.blacklist_exceptions.Prepare();
  return config;
}

}  // namespace cdbg
}  // namespace devtools
