use std::{fmt::Display, str::FromStr};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Profile { #[default] Debug, Release, Final }

impl FromStr for Profile {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "debug"   => Ok(Profile::Debug),
            "release" => Ok(Profile::Release),
            "final"   => Ok(Profile::Final),
            profile => Err(format!("unknown profile: {profile} (expected debug, release or final)"))
        }
    }
}

impl Display for Profile {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Profile::Debug   => write!(f, "debug"),
            Profile::Release => write!(f, "release"),
            Profile::Final   => write!(f, "final"),
        }
    }
}

impl Profile {
    pub fn cmake_config(self) -> &'static str {
        match self {
            Profile::Debug   => "Debug",
            Profile::Release => "Release",
            Profile::Final   => "Final",
        }
    }

    pub fn cargo_args(self) -> &'static [&'static str] {
        match self {
            Profile::Debug   => &[],
            Profile::Release => &["--release"],
            Profile::Final   => &["--release"],
        }
    }

    pub fn cargo_env(self) -> &'static [(&'static str, &'static str)] {
        match self {
            Profile::Debug   => &[],
            Profile::Release => &[("CARGO_PROFILE_RELEASE_DEBUG", "true" )],
            Profile::Final   => &[("CARGO_PROFILE_RELEASE_DEBUG", "false"),
                                  ("CARGO_PROFILE_RELEASE_LTO"  , "true" ),
                                  ("CARGO_PROFILE_RELEASE_STRIP", "true" )],

        }
    }
}

#[cfg(test)]
mod tests {
    use std::str::FromStr;
    use crate::profile::Profile;

    #[test]
    fn profile_from_str() {
        assert_eq!(Profile::from_str("debug")  , Ok(Profile::Debug));
        assert_eq!(Profile::from_str("release"), Ok(Profile::Release));
        assert_eq!(Profile::from_str("final")  , Ok(Profile::Final));
        assert!(Profile::from_str("unknown").is_err());
    }

    #[test]
    fn profile_display_format() {
        for profile in [Profile::Debug, Profile::Release, Profile::Final] {
            assert_eq!(profile.to_string().parse::<Profile>(), Ok(profile));
        }
    }

    #[test]
    fn profile_cmake_config() {
        assert_eq!(Profile::Debug.cmake_config()  , "Debug");
        assert_eq!(Profile::Release.cmake_config(), "Release");
        assert_eq!(Profile::Final.cmake_config()  , "Final");
    }

    #[test]
    fn profile_cargo_args() {
        let debug_args: &[&str] = &[];
        let release_args: &[&str] = &["--release"];
        let final_args: &[&str] = &["--release"];
        assert_eq!(Profile::Debug.cargo_args()  , debug_args);
        assert_eq!(Profile::Release.cargo_args(), release_args);
        assert_eq!(Profile::Final.cargo_args()  , final_args);
    }

    #[test]
    fn profile_cargo_env() {
        let debug_env:   &[(&str, &str)] = &[];
        let release_env: &[(&str, &str)] = &[("CARGO_PROFILE_RELEASE_DEBUG", "true")];
        let final_env:   &[(&str, &str)] = &[("CARGO_PROFILE_RELEASE_DEBUG", "false"),
                                             ("CARGO_PROFILE_RELEASE_LTO"  , "true" ),
                                             ("CARGO_PROFILE_RELEASE_STRIP", "true" )];
        assert_eq!(Profile::Debug.cargo_env()  , debug_env);
        assert_eq!(Profile::Release.cargo_env(), release_env);
        assert_eq!(Profile::Final.cargo_env()  , final_env);
    }

    #[test]
    fn final_enables_lto_and_strip() {
        let env = Profile::Final.cargo_env();
        assert!(env.contains(&("CARGO_PROFILE_RELEASE_LTO", "true")));
        assert!(env.contains(&("CARGO_PROFILE_RELEASE_STRIP", "true")));
    }

    #[test]
    fn release_keeps_symbols() {
        assert!(Profile::Release.cargo_env().contains(&("CARGO_PROFILE_RELEASE_DEBUG", "true")));
    }

    #[test]
    fn cargo_env_only_uses_known_keys() {
        // Cargo ignores unknown CARGO_PROFILE_* variables silently: this is what catches a typo.
        const KNOWN: [&str; 3] = ["CARGO_PROFILE_RELEASE_DEBUG", "CARGO_PROFILE_RELEASE_LTO", "CARGO_PROFILE_RELEASE_STRIP"];
        for profile in [Profile::Debug, Profile::Release, Profile::Final] {
            for (key, _) in profile.cargo_env() {
                assert!(KNOWN.contains(key), "{profile}: unknown cargo variable {key}");
            }
        }
    }
}